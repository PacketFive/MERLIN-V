# `merlin-sign` — prototype signed-program tool

The fourth user-space code prototype of MERLIN-V.  Implements
**MVCP signed programs** per
[`../../docs/design/09-mvcp-kernel-uapi.md`](../../docs/design/09-mvcp-kernel-uapi.md) §3.1
— hash the canonical signed region of a MERLIN-V ELF, sign it
with ed25519, embed a `.merlin.sig` section.

This is the userspace half of MVCP layer-A signed-program loader
gating.  The matching kernel-side enforcement (the
`kernel.merlin_require_signed` sysctl, kernel keyring binding, and
load-time verification) will live in `kernel/merlin/` when
`proto-kernel-loader` lands.  Until then, the ed25519 verify path
in this tool stands in.

## Subcommands

```
merlin-sign sign -k <priv.pem> -i <key_id> <in.o> <out.o>
merlin-sign verify -k <pub.pem> <in.o>
merlin-sign tag <in.o>             # SHA-256 of canonical signed region
merlin-sign dump <in.o>            # print .merlin.sig header
```

## Wire format

`.merlin.sig` is exactly the layout pinned in `09-mvcp-kernel-uapi.md`
§3.1.1: a `struct merlin_sig_v1` header (40 bytes) followed by the
signature bytes (64 for ed25519).

The hashed region is the **canonical concatenation** of:

```
.merlin.meta, .merlin.maps, .merlin.relocs,
.merlin.btf, .merlin.btf_ext, .merlin.license,
then every .text.merlin.* in section-header-table order.
```

The hash a program is *tagged* by — the `prog_tag` in MVCP
attestation quotes — is the SHA-256 of this byte stream.  The same
hash is what `merlin-objtool sha256` prints; the test battery
asserts byte-for-byte equality between the two tools.

## Build

```bash
sudo apt install libelf-dev libssl-dev
make
make test     # runs 8-case battery using openssl genpkey for keys
```

## Test battery

```
[PASS] tag deterministic                       # two `tag` runs agree
[PASS] tag matches merlin-objtool sha256       # cross-tool agreement
[PASS] sign+verify with matching keys
[PASS] tag unchanged after signing             # .merlin.sig not self-hashed
[PASS] verify with wrong key rejected
[PASS] verify rejects content mutation         # flip one byte in license
[PASS] verify rejects unsigned object
[PASS] dump prints algo + sig_bytes_len
```

The "tag unchanged after signing" case proves the subtle invariant
that signing must not change the hash — i.e. `.merlin.sig` is correctly
excluded from the canonical section list of *itself*.

## What's not yet implemented

- ECDSA-P256 and RSA-PSS-2048 sign/verify (v0 = ed25519 only).
- Kernel keyring integration; the policy-gating sysctl
  `kernel.merlin_require_signed` is a kernel-side concern that will
  land with `proto-kernel-loader`.
- Hardware-rooted attestation key (MAK lifecycle and HW chain are
  separate work, tracked by `proto-mvcp-attestation`).
- Multi-signature support (today an object carries at most one
  signature; future revisions may support a chain of signatures
  from a build pipeline).

## Relationship to other tools

```
.merlin.o
   |   compile (off-target ok)
   v
merlin-objtool validate          [prototype-1]
   |
   v
merlin-sign sign -k <priv>       [prototype-4, this directory]
   |   embed .merlin.sig
   v
.merlin.signed.o
   |
   |   (eventual flow:)
   v
kernel: MERLIN_PROG_LOAD
   |   load-time enforcement against kernel keyring
   v
merlin-verifier (in-kernel)      [prototype-2 ported]
   |
   v
merlin-jit-x86_64                [prototype-3 ported]
   |
   v
PROT_EXEC native code
```

## Library

`sig.h` exposes a tidy library API (`merlin_sig_hash_signed_region`,
`merlin_sig_sign_ed25519`, `merlin_sig_verify_ed25519`,
`merlin_sig_embed`, `merlin_sig_extract`) so the eventual
`proto-kernel-loader` work can reuse the verify path without
re-implementing the hash/parse logic.  When the in-tree work lands,
the verify and extract paths port to kernel code; the sign-side
remains user-space.
