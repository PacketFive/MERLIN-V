# 09 — MVCP Layer A: in-kernel UAPI primitives

*Status: draft. The user-visible API surface for MVCP — the control
plane primitives MERLIN-V ships as kernel UAPI.  **User-space
prototypes landed: signed programs (tools/merlin-sign/, §3.1),
standard telemetry (tools/merlin-telemetry/, §3.5), and program
namespaces (tools/merlin-ns/, §3.4) — all under
`uapi/merlin/*.h` canonical headers.***

## 1. What MVCP is

**MVCP** (MERLIN-V Control Plane) is the set of kernel-side
primitives that lets a controller — running locally on the host or
remotely in a hyperscale fleet — manage MERLIN-V programs, maps,
namespaces, and telemetry safely.

This document specifies *layer A*: the in-kernel UAPI. The
companion documents specify the higher-level pieces:

- [`10-mvcp-daemon-and-fleet.md`](10-mvcp-daemon-and-fleet.md) —
  `merlind`, a reference out-of-tree daemon that consumes layer A
  and exposes fleet semantics (rolling deploy, canary, telemetry
  aggregation) over gRPC/HTTPS.
- [`11-mvcp-attestation.md`](11-mvcp-attestation.md) — the
  attestation protocol in detail: quote format, key hierarchy,
  TEE/HW chain on capable targets, verification.

## 2. Design principles

MVCP is shaped by three observations from a decade of eBPF
fleet-management practice:

1. **Every hyperscaler builds the same daemon.** Cilium operator,
   Meta's BPF deploy pipeline, Cloudflare's `bpfman`, k8s
   eBPF-operator pattern — they all do the same five things
   (signed-deploy, canary, telemetry, atomic-update, multi-tenant
   isolation) on top of `bpf()`. MERLIN-V ships those five things
   as kernel primitives so the daemon becomes a thin facade rather
   than a substantial reimplementation per organisation.
2. **Fleet semantics need kernel-side guarantees.** Atomic
   multi-map update, attestation, and signed-load gating cannot be
   correctly implemented purely in userspace. They need primitives
   the kernel enforces.
3. **Telemetry must be opt-out, not opt-in.** In eBPF every
   program reinvents counter maps. MVCP makes a standard counter
   set the *default*; programs may emit additional counters but
   the basics are guaranteed.

## 3. The five primitives

### 3.1 Signed programs

#### 3.1.1 Wire format

A signature lives in a new ELF section `.merlin.sig`:

```c
#define MERLIN_SIG_MAGIC    0x47495356u   /* 'VSIG' little-endian */

struct merlin_sig_v1 {
    __u32 magic;            /* MERLIN_SIG_MAGIC                       */
    __u32 sig_size;         /* this struct size for forward compat    */
    __u32 algo;             /* enum merlin_sig_algo                   */
    __u32 key_id;           /* opaque key identifier; matches keyring */
    __u64 signed_blob_off;  /* file offset of the first byte signed   */
    __u64 signed_blob_len;  /* length of the signed region            */
    __u32 sig_bytes_len;    /* length of signature in trailing bytes  */
    __u32 _reserved;
    /* signature bytes follow, sig_bytes_len long */
} __attribute__((packed));

enum merlin_sig_algo {
    MERLIN_SIG_ALGO_ED25519     = 1,
    MERLIN_SIG_ALGO_ECDSA_P256  = 2,
    MERLIN_SIG_ALGO_RSA_PSS_2048= 3,
};
```

The signed region covers `.merlin.meta`, `.text*`, `.merlin.maps`,
`.merlin.relocs`, `.merlin.btf`, `.merlin.btf_ext`,
`.merlin.license` — every section that affects program
semantics. The signature itself, ELF section header tables, and
`.shstrtab` are excluded.

These sections are typically *not* contiguous in the on-disk
ELF (section headers and `.shstrtab` fall between them), so the
hashed input is the **canonical concatenation** of the section
*data*, in this fixed order:

```
.merlin.meta, .merlin.maps, .merlin.relocs,
.merlin.btf, .merlin.btf_ext, .merlin.license,
then every .text.merlin.* in section-header-table order.
```

The `signed_blob_off` and `signed_blob_len` fields of
`struct merlin_sig_v1` are interpreted under this scheme as:

- `signed_blob_off == 0` — *canonical-section-list mode*. The
  signed input is the canonical concatenation defined above.
  `signed_blob_len` records the total byte length of that
  concatenation (a useful cross-check at verification time).
- `signed_blob_off != 0` — *single-region mode* (reserved for
  future use, e.g. out-of-line attestation blobs).

RFC v1 implementations emit and accept only the canonical-list
mode. The prototype tool is `tools/merlin-sign/`; the digest it
computes is identical to `merlin-objtool sha256`.

#### 3.1.2 Keyring binding

The trust root is a kernel keyring. The new command:

```
MERLIN_KEYRING_BIND
    namespace_fd  __u32   (0 = global)
    keyring_id    __s32   key_serial_t of a "trusted" keyring
    flags         __u32   reserved, 0
```

This associates a trust-root keyring with a MERLIN namespace
(§3.4). Only keys in that keyring (and its sub-keyrings) can sign
programs accepted in that namespace. The keyring uses the existing
`keys/trusted` infrastructure — admins manage it with `keyctl`.

#### 3.1.3 Load-time enforcement

A new sysctl:

```
kernel.merlin_require_signed = 0   /* permissive: warn on unsigned */
                             = 1   /* enforce: reject unsigned     */
                             = 2   /* strict: reject unsigned and
                                      reject signed-but-untrusted  */
```

`MERLIN_PROG_LOAD` consults the sysctl and the calling namespace's
trust-root keyring. On `= 2`, only programs whose `.merlin.sig`
verifies against a trusted key are admitted. On `= 1`, signed
programs are verified (and rejected if invalid) but unsigned
programs are also admitted with a warning logged. On `= 0`, only
syntactic checks happen.

Per-namespace override: each MERLIN namespace can tighten (not
relax) the global policy via a per-namespace knob.

### 3.2 Load attestation

A controller deploying program version *X* to *N* hosts needs to
*verify* each host loaded exactly *X*. MVCP provides a kernel-
signed quote:

```c
struct merlin_attestation_v1 {
    __u32 size;                 /* this struct size, forward compat   */
    __u32 prog_id;
    __u8  prog_tag[32];         /* SHA-256 of the signed region       */
    __u64 load_time_ns;         /* CLOCK_BOOTTIME at load             */
    __u32 ns_id;                /* MERLIN namespace id                */
    __u32 profile;              /* enum merlin_profile                */
    __u8  kernel_build_id[20];  /* matches /sys/kernel/notes          */
    __u32 attesting_key_id;     /* which kernel key signed this quote */
    __u32 attestation_algo;     /* enum merlin_sig_algo               */
    __u8  nonce[32];            /* echoed from request                */
    /* signature bytes follow                                          */
};
```

Command:

```
MERLIN_PROG_GET_ATTESTATION
    prog_fd       __u32
    nonce_ptr     __u64  user ptr to 32-byte caller nonce
    out_buf       __u64  user ptr to receive merlin_attestation_v1+sig
    out_buf_len   __u32  IN: capacity; OUT: actual bytes written
    flags         __u32  reserved
```

The attesting key is a kernel-internal key created at boot from a
hardware root if available (TPM, ARM CCA, Intel TDX, Keystone-style
TEE on RISC-V), or from a kernel-generated ephemeral key if not.
The full key hierarchy is specified in
[`11-mvcp-attestation.md`](11-mvcp-attestation.md).

The nonce is mandatory and must be controller-fresh; it prevents
replay of stale attestations during incident investigations.

### 3.3 Atomic map-batch transactions

Hyperscale rollouts often need to flip *several* maps together
(e.g. an allowlist plus a corresponding rate-limit map) atomically.
Today this requires per-CPU shadow staging and userspace locking.
MVCP provides kernel-side transactions:

```
MERLIN_MAP_BATCH_TXN_BEGIN
    timeout_ns    __u64   how long the txn may stay open
    flags         __u32
    txn_fd        __u32   OUT: file descriptor referencing the txn

MERLIN_MAP_BATCH_TXN_STAGE
    txn_fd        __u32
    map_fd        __u32
    op            __u32   MERLIN_MAP_TXN_{INSERT, UPDATE, DELETE, REPLACE_ALL}
    key_ptr       __u64
    key_len       __u32
    value_ptr     __u64
    value_len     __u32

MERLIN_MAP_BATCH_TXN_COMMIT
    txn_fd        __u32
    flags         __u32   MERLIN_TXN_F_DRAIN_RCU (default), F_FAST

MERLIN_MAP_BATCH_TXN_ABORT
    txn_fd        __u32
```

Semantics:

- A staged operation is invisible to programs until commit.
- Commit either applies all staged ops or applies none (returns
  `-ECONFLICT` on any conflicting concurrent commit).
- Across maps, a single commit is observed atomically by any
  program currently running: the program either sees the
  pre-commit state in all touched maps or the post-commit state
  in all touched maps. The implementation uses a per-CPU
  versioned snapshot pointer for each touched map (the same
  pattern eBPF uses for RCU-protected hashmaps, generalised).
- `MERLIN_TXN_F_DRAIN_RCU` (default) waits for an RCU grace
  period after commit before returning, guaranteeing no in-flight
  program is still observing the pre-commit state on return.
- `MERLIN_TXN_F_FAST` skips the drain; userspace takes
  responsibility for any required quiesce.

Concurrent commits on overlapping map sets are serialised on a
per-namespace transaction lock; commits on disjoint map sets do
not contend.

Lifetime: the transaction is held by `txn_fd`; closing the fd
without commit implies abort. There is a kernel-enforced upper
bound on staged ops per txn (initial value 4096; sysctl tunable).

### 3.4 Program namespaces (`MERLIN_NSFS`)

A new kernel namespace type — parallel to netns and pidns — that
scopes:

- **Hook surfaces.** A namespace declares which `MERLIN_ATTACH_*`
  attach types its programs may use. Default for an unprivileged
  namespace: none. Default for a privileged namespace: all.
- **kfunc visibility.** A namespace has an allowlist of kfunc
  type-IDs. A program in a namespace whose kfunc visibility set
  does not include a referenced kfunc is rejected at load.
- **Helper visibility.** A subset of the global helper set is
  exposed per namespace.
- **Trust root.** Each namespace has its own trust-root keyring
  for signed-program enforcement.
- **Map quotas.** Maximum map memory, maximum program count.

Commands:

```
MERLIN_NS_CREATE
    parent_ns_fd  __u32   0 = inherit from caller
    config_ptr    __u64   user ptr to struct merlin_ns_config_v1
    config_len    __u32
    flags         __u32   reserved
    ns_fd         __u32   OUT: file descriptor referencing the new ns

MERLIN_NS_GET_FD_BY_ID
    ns_id         __u32
    open_flags    __u32
```

Namespace inheritance: a process entering a MERLIN namespace via
`setns(merlin_ns_fd, CLONE_MERLIN_NEWNS)` inherits its
restrictions. Programs loaded in a namespace are confined to it.
Programs in *parent* namespaces are visible read-only via
`MERLIN_PROG_GET_NEXT_ID` if a hierarchy flag permits.

The `merlinfs` filesystem (parallel to bpffs) gains a per-namespace
mount; pinned objects are namespace-scoped by mount path.

#### 3.4.1 Interaction with existing kernel namespaces

A MERLIN namespace is a sibling, not a child, of netns / pidns /
mntns. The MVDP install path (§3 of [`08-mvdp-and-af-mvdp.md`](08-mvdp-and-af-mvdp.md))
gates on **both** the MERLIN namespace (does this program have
`MERLIN_ATTACH_MVDP` permission?) and the netns (does the caller
have `CAP_NET_ADMIN` on the target netdev's netns?). This double
gate is intentional — it lets hyperscale operators delegate
"can run MERLIN-V programs" without delegating "can configure
network interfaces."

### 3.5 Standard telemetry export

Every loaded program automatically gains a per-CPU counter block.
The block layout is fixed UAPI.  See the canonical header
[`uapi/merlin/stats.h`](uapi/merlin/stats.h):

```c
#define MERLIN_STATS_V1_VERDICTS 8

struct merlin_prog_stats_v1 {
    __u32 size;                 /* sizeof(struct) as emitted; forward compat */
    __u32 _pad;
    __u64 run_count;            /* number of times the program ran           */
    __u64 run_ns_total;         /* total ns spent inside the program         */
    __u64 verdict_count[MERLIN_STATS_V1_VERDICTS];
                                /* indexed by enum mvdp_action / equivalent  */
    __u64 helper_call_count;
    __u64 helper_fault_count;
    __u64 verifier_load_ns;     /* one-shot, never updated again             */
    __u64 last_run_time_ns;     /* CLOCK_BOOTTIME ns of last invocation      */
    __u64 _reserved[4];
};
```

Counters are updated by the dispatch shim, not by the program
itself. The program pays no instructions; the shim pays one add
per counter on the run-count and total-ns, plus one indexed add on
the verdict.

Three access paths:

1. **`tracefs`.** A per-program directory
   `/sys/kernel/tracing/merlin/<prog_tag>/stats` (or
   `merlin/<prog_id>/` if tag unavailable) exposes the counters as
   human-readable text and machine-readable binary (`stats_raw`).
2. **Syscall.** `MERLIN_PROG_GET_INFO_BY_FD` returns
   `merlin_prog_stats_v1` aggregated across CPUs as a trailing
   field in `struct merlin_prog_info`.
3. **Multicast.** Per-program counters are streamed on
   `MERLIN_NL_CTRL` at a configurable interval (default 10s, off
   if zero); see §4.

### 3.6 Multicast control channel (`MERLIN_NL_CTRL`)

A new netlink family. One multicast group; events authenticated
by sender capability (must be `CAP_MERLIN` or in a namespace where
the operation is permitted).

Event types (`enum merlin_nl_event`):

```
MERLIN_NL_EV_PROG_LIFECYCLE   { ns_id, prog_id, action, tag, time }
MERLIN_NL_EV_MAP_UPDATE       { ns_id, map_id, txn_id, op, ... }
MERLIN_NL_EV_TELEMETRY_SAMPLE { ns_id, prog_id, stats }
MERLIN_NL_EV_ATTESTATION_REQ  { nonce, target_prog_id }
MERLIN_NL_EV_ATTESTATION_RESP { quote }
MERLIN_NL_EV_NAMESPACE        { ns_id, action }
```

The channel is **strictly informational and request/response**;
it never carries new authority beyond what `bpf()`-style command
calls already give. A controller listens for telemetry and emits
attestation requests; it cannot, via this channel, modify program
or map state. State changes always go through `merlin(2)`. This
keeps the security boundary clean: the multicast group is
read-mostly with capability-gated `_REQ` events.

## 4. New `merlin_cmd` numbers

Added to `enum merlin_cmd` in [`uapi/linux/merlin.h`](uapi/linux/merlin.h):

| Number | Command | §  |
| ------ | ------- | -- |
| 20 | `MERLIN_PROG_GET_ATTESTATION` | 3.2 |
| 21 | `MERLIN_MAP_BATCH_TXN_BEGIN`  | 3.3 |
| 22 | `MERLIN_MAP_BATCH_TXN_STAGE`  | 3.3 |
| 23 | `MERLIN_MAP_BATCH_TXN_COMMIT` | 3.3 |
| 24 | `MERLIN_MAP_BATCH_TXN_ABORT`  | 3.3 |
| 25 | `MERLIN_NS_CREATE`            | 3.4 |
| 26 | `MERLIN_NS_GET_FD_BY_ID`      | 3.4 |
| 27 | `MERLIN_KEYRING_BIND`         | 3.1 |

The corresponding sub-structs land in the `union merlin_attr` in
the next revision of [`uapi/linux/merlin.h`](uapi/linux/merlin.h);
the enum numbers are already reserved.

## 5. What ships in RFC v1 prototype

All of layer A implements as in-kernel prototype code:

- Signed-program loader gating + sysctl
- Per-program standard telemetry counters + tracefs export
- Program namespaces with hook / kfunc / helper / quota scoping
- Load attestation (kernel-key-only on host CPU; HW-root chain
  stubbed for v1, implemented when first TEE-capable target lands)
- Atomic map-batch transactions
- `MERLIN_NL_CTRL` netlink-multicast channel

These are tracked in SQL todos: `proto-mvcp-signed-progs`,
`proto-mvcp-telemetry`, `proto-mvcp-namespaces`,
`proto-mvcp-attestation`, `proto-mvcp-batch-txn`,
`proto-mvcp-multicast`.

## 6. Open items

- **Attestation key bootstrap on platforms without HW root.** The
  v1 design uses a kernel-generated key persisted via tpm-emulator
  or a config-supplied seed. The threat model is documented in
  [`11-mvcp-attestation.md`](11-mvcp-attestation.md).
- **Cross-namespace map sharing.** Whether two namespaces can
  share a map by passing the fd (yes; this is unavoidable given
  fd-passing semantics) versus by a controlled "expose" operation.
  Default for v1 is "fd-passing works; quota counts against
  whichever ns *created* the map."
- **Telemetry counter overflow.** 64-bit counters never overflow
  in practice but exporters need defined wrap semantics. Document
  as "do not assume monotonic across very long uptimes; use
  deltas."
- **Streaming format for `MERLIN_NL_EV_TELEMETRY_SAMPLE`.**
  Whether to compress (lz4) or send raw; v1 sends raw.
- **Versioning policy for `merlin_prog_stats_v1`.** Future
  extensions append fields; the `size` prefix already supports
  this. We commit to never changing the meaning of existing
  fields.
