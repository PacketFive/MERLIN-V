# 11 — MVCP Attestation

*Status: draft. The attestation protocol that backs
[`09-mvcp-kernel-uapi.md`](09-mvcp-kernel-uapi.md) §3.2.
**User-space prototype landed (tools/merlin-attest/) — MAK
lifecycle, ed25519 quote sign/verify, replay defence via
`quote_seq`. HW chain reserved but not yet implemented.***

## 1. What we are attesting to

An attestation answers exactly this question, asked by a remote
controller about a specific host:

> *"Did you, host H, at time T, load the program whose signed
> region hashes to TAG into namespace NS on a kernel whose build
> ID is BID, and is the program still there?"*

Nothing more. The attestation is not a general remote-execution
trust framework; it is not a replacement for TPM remote attestation
of the host's boot state (those frameworks compose orthogonally);
and it does not attest to runtime behaviour — only to load-time
state.

The narrowness is deliberate. A controller wanting "did this
deploy actually land on the host" needs only this much, and over-
scoping attestation has been a recurring failure mode of analogous
systems.

## 2. Quote format

```c
#define MERLIN_ATTESTATION_MAGIC    0x54534D4Bu   /* 'KMST' little-endian */
                                                 /* (Keystone-MERLIN ATtest) */

struct merlin_attestation_v1 {
    __u32 magic;                /* MERLIN_ATTESTATION_MAGIC               */
    __u32 size;                 /* this struct size, forward compat       */
    __u32 version;              /* 1                                      */
    __u32 algo;                 /* enum merlin_sig_algo                   */

    /* Subject: what is being attested */
    __u32 prog_id;
    __u32 ns_id;
    __u32 profile;              /* enum merlin_profile                    */
    __u32 _pad0;
    __u8  prog_tag[32];         /* SHA-256 of the signed region of ELF    */

    /* Context */
    __u64 load_time_ns_boot;    /* CLOCK_BOOTTIME at MERLIN_PROG_LOAD     */
    __u64 attestation_time_ns_boot;
    __u64 quote_seq;            /* monotonic per-key; replay defence      */
    __u8  kernel_build_id[20];  /* matches /sys/kernel/notes              */
    __u8  _pad1[4];

    /* Identity of the key that signed THIS quote */
    __u32 attesting_key_id;
    __u32 attesting_key_algo;   /* enum merlin_sig_algo                   */
    __u8  attesting_key_pub_hash[32];   /* SHA-256 of pub key DER         */

    /* HW chain (zero-filled if no HW root available) */
    __u32 hw_chain_present;     /* 0 or 1                                 */
    __u32 hw_chain_kind;        /* enum merlin_hw_chain_kind              */
    __u32 hw_chain_offset;      /* offset to HW evidence blob, from start */
    __u32 hw_chain_len;
    __u8  hw_chain_digest[32];  /* SHA-256 of HW evidence                 */

    /* Challenge */
    __u8  nonce[32];            /* echoed from MERLIN_PROG_GET_ATTESTATION*/

    /* Signature */
    __u32 sig_offset;           /* offset to signature bytes              */
    __u32 sig_len;
};

enum merlin_hw_chain_kind {
    MERLIN_HW_CHAIN_NONE        = 0,
    MERLIN_HW_CHAIN_TPM_2_0     = 1,    /* TPM 2.0 quote, NV index ... */
    MERLIN_HW_CHAIN_ARM_CCA     = 2,    /* ARM CCA realm attestation   */
    MERLIN_HW_CHAIN_INTEL_TDX   = 3,    /* TDX quote                   */
    MERLIN_HW_CHAIN_AMD_SEV_SNP = 4,    /* SEV-SNP attestation report  */
    MERLIN_HW_CHAIN_KEYSTONE_TEE= 5,    /* RISC-V Keystone attestation */
    MERLIN_HW_CHAIN_RISCV_AIA   = 6,    /* RISC-V AIA / Sm-CSR-based   */
};
```

The signed bytes are `[0, sig_offset)`; the signature occupies
`[sig_offset, sig_offset + sig_len)`. Bytes after that are HW
evidence if present, addressed by `hw_chain_offset / len`.

## 3. Key hierarchy

```
           +-------------------------------------------+
           |  Hardware root of trust (optional)        |
           |  TPM EK, CCA RAK, TDX MRTD, SEV-SNP VCEK, |
           |  Keystone DRK, ...                        |
           +----------------------+--------------------+
                                  |
                          (HW-attested)
                                  v
           +-------------------------------------------+
           |  MERLIN attestation key (MAK)             |
           |  - generated at kernel boot               |
           |  - ed25519 by default                     |
           |  - pubkey published via sysfs at          |
           |    /sys/kernel/merlin/mak.pub             |
           |  - private key never leaves kernel        |
           +----------------------+--------------------+
                                  |
                            (MAK-signed)
                                  v
           +-------------------------------------------+
           |  Quote (struct merlin_attestation_v1)     |
           +-------------------------------------------+
```

### 3.1 MAK lifecycle

- **Boot.** The kernel generates a fresh MAK on first
  `MERLIN_PROG_GET_ATTESTATION` after boot, or eagerly at
  module init if a sysctl says so. The MAK is keyed in the
  kernel's keyring with a fixed description.
- **HW binding.** If a HW root is available, the kernel
  requests an attestation of the MAK pubkey from the HW
  root. The resulting evidence is embedded in every quote
  this MAK signs (the `hw_chain_*` fields).
- **Persistence.** By default the MAK is volatile (lost on
  reboot). Controllers re-pin pubkeys after each reboot. A
  sysctl allows sealing the MAK to a TPM NV index for
  persistence across reboots on hosts that support it; this
  is opt-in because it ties MERLIN state to TPM availability.
- **Rotation.** A new `MERLIN_KEYRING_ROTATE_MAK` ioctl
  (TBD command number) destroys the current MAK and
  generates a new one. After rotation, old quotes are still
  cryptographically valid (signatures don't expire) but
  controllers consulting `mak.pub` will see the new key.
  This is the manual escape hatch if a MAK is suspected
  compromised.

### 3.2 Replay defence

Two mechanisms:

1. **Nonce.** Controllers MUST supply a fresh nonce per
   request. Replayed quotes can be detected by nonce
   mismatch.
2. **`quote_seq`.** A monotonically increasing counter per
   MAK. Controllers store the highest seq seen per
   `(host, prog_id)` and reject quotes with seq <=
   previously-seen-seq. This catches a controller-confused
   replay where the right nonce is somehow obtained.

The kernel implementation of `quote_seq` is a per-MAK atomic
counter incremented on each quote signature.

## 4. Verification protocol

A controller verifying a quote performs:

1. **Magic + version + size check.** Reject if `magic !=
   MERLIN_ATTESTATION_MAGIC` or `version` is unknown or `size`
   is unreasonable.
2. **Nonce check.** `nonce` must equal the controller-supplied
   nonce from this request.
3. **Signature check.** Verify `[0, sig_offset)` under the
   MAK identified by `attesting_key_id` /
   `attesting_key_pub_hash`. The MAK pubkey is obtained
   out-of-band: either from `/sys/kernel/merlin/mak.pub`
   over an authenticated channel, or from a controller-side
   pin established at host registration.
4. **HW chain check (if expected).** If the controller's
   policy requires `hw_chain_present == 1`, verify the HW
   evidence blob according to its kind. The MAK pubkey hash
   must appear in the HW evidence (the binding from step
   §3.1).
5. **Subject check.** `prog_tag` must equal the SHA-256 of
   the controller's expected program signed region;
   `kernel_build_id` must match the expected host kernel
   (if pinned); `ns_id` must match the expected namespace.
6. **Replay check.** `quote_seq > last_seen_seq` for this
   (host, MAK).

Failure of any step yields *no information* to the caller
beyond "verification failed" — the controller does not log
which specific check failed (timing-side-channel resistance);
that detail lives only in the controller's local logs.

## 5. HW chain details

### 5.1 None

The default when no HW root is available. The MAK is a
kernel-generated ephemeral key; trust is exactly "the kernel
that booted on this host". This is acceptable for many
controllers — they already trust the boot chain (signed
kernel + signed initramfs + signed root FS) and just want
to know which program is loaded.

### 5.2 TPM 2.0

`hw_chain_kind = MERLIN_HW_CHAIN_TPM_2_0`. The HW evidence
blob is a `TPM2_Quote` covering PCRs the kernel has
extended with the MAK pubkey hash plus the kernel build ID.
The default PCR is 12 (one of the resettable application
PCRs); configurable via sysctl.

### 5.3 Keystone TEE (RISC-V)

`hw_chain_kind = MERLIN_HW_CHAIN_KEYSTONE_TEE`. The HW
evidence is a Keystone attestation report binding the MAK
pubkey to the device root key (DRK) and the security
monitor measurement.

This is the natural HW chain on TEE-capable RISC-V targets,
including future MERLIN-V hardware acceleration targets that
ship with Keystone. The KESTREL-V doctoral project at the
University of Limerick — independent of MERLIN-V — uses
the same Keystone evidence format for its eBPF accelerator
slots; we deliberately keep the wire format compatible so
that controllers verifying KESTREL-V and MERLIN-V can share
verification code without depending on either project.

### 5.4 ARM CCA / Intel TDX / AMD SEV-SNP

Standard cloud-attestation formats; the HW evidence blob is
the platform-issued attestation report with the MAK pubkey
hash carried in the runtime data field.

### 5.5 RISC-V AIA / Sm-CSR

A placeholder for emerging RISC-V security-extension based
attestation that does not require Keystone. Format TBD with
the upstream RISC-V security task group.

## 6. Threat model

What attestation defends against:

- **Mis-deploy.** Controller thinks it shipped program X to host
  H; it actually shipped program Y. Nonce + tag check catches
  this.
- **Replay.** Attacker captures a valid quote and replays it to
  fool a later controller query. Nonce + `quote_seq` catches
  this.
- **Cross-host confusion.** Quote from host A delivered as if
  from host B. The MAK pubkey is per-host; pubkey pin catches
  this.
- **Stale-program rollback attack.** Attacker convinces a host
  to load an old (but still validly signed) program version.
  Tag-pin policy catches this if the controller enforces a
  minimum version.

What it does **not** defend against:

- **Compromised kernel.** A kernel that lies about its loaded
  programs can produce arbitrary quotes. HW chain (§5) is the
  defence; without HW chain, kernel-level trust is the bound.
- **Compromised MAK at HW level.** A physically-attacked TPM
  or TEE can leak the HW root; HW vendor's threat model.
- **Side channels.** Attestation doesn't defend against
  runtime-behaviour leakage from the program itself.
- **Long-term cryptanalysis.** ed25519 is the default; algorithm
  agility via `algo` field. No post-quantum option today; one
  will be added when NIST PQC primitives settle.

## 7. Integration with MVCP namespaces

Each MERLIN namespace records, at namespace creation, the
expected `hw_chain_kind` policy:

```
enum merlin_ns_attest_policy {
    MERLIN_NS_ATTEST_NONE        = 0,  /* HW chain optional      */
    MERLIN_NS_ATTEST_HW_RECOMMENDED = 1, /* warn if missing      */
    MERLIN_NS_ATTEST_HW_REQUIRED = 2,  /* attestation requests
                                          fail if HW chain N/A   */
};
```

A high-security namespace ("payment-processing") might be
created with `HW_REQUIRED`; a development namespace with
`NONE`. The namespace's policy is itself part of every quote
implicitly via the binding from MAK → kernel state →
namespace; explicit `attest_policy` is also a field reserved
for future inclusion in `merlin_attestation_v1`.

## 8. Open items

- **MAK protection on shared-tenant cloud hosts.** When the
  host kernel is not under operator control (e.g. hosted by a
  cloud provider), MAK signatures attest to "what this
  provider-controlled kernel says happened" — that is
  arguably less than what the controller wants. HW chain to
  CCA/TDX/SEV-SNP is the answer on those platforms, but
  composition with the cloud provider's existing attestation
  flow needs spec work. We defer to v2.
- **MAK pubkey distribution.** `/sys/kernel/merlin/mak.pub`
  is per-host; controllers must learn it. Options: out-of-band
  registration, multicast announcement on `MERLIN_NL_CTRL`,
  or fetching during the first attestation (TOFU). v0 leans
  out-of-band registration.
- **Cross-version quote compatibility.** If a controller is
  reading `_v1` and the host signs `_v2`, the size field
  lets the controller parse known fields. We commit to never
  changing existing field semantics.
- **Whether `prog_id` should be the load-time id (per-host,
  not portable) or the `prog_tag` only.** v0 includes both
  because `prog_id` is convenient for local correlation and
  `prog_tag` is the cryptographic identity. Controllers that
  care only about cryptographic identity ignore `prog_id`.
- **Quote-bundling.** A controller asking about *N* programs
  on the same host can request all *N* quotes in one syscall
  to amortise crossing cost. Reserved for v2.
