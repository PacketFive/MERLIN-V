# `merlin-attest` — prototype MVCP attestation

The sixth user-space code prototype of MERLIN-V.  Implements
**MVCP load attestation** per
[`../../docs/design/11-mvcp-attestation.md`](../../docs/design/11-mvcp-attestation.md)
— the kernel-signed quote that a controller uses to confirm a
host loaded a specific MERLIN-V program.

## What the prototype proves

- The canonical quote struct `merlin_attestation_v1` is now
  pinned in a real header
  ([`../../docs/design/uapi/merlin/attestation.h`](../../docs/design/uapi/merlin/attestation.h)),
  240 bytes for v1.
- The **MAK lifecycle** (generate, persist, hash pub, increment
  seq) works end-to-end via OpenSSL EVP / ed25519.
- The **replay defence** based on `quote_seq` is verifiable in
  user space: the verifier rejects any quote whose `seq` is `<=`
  the highest `seq` it has previously seen for the MAK.
- The **nonce + signature + key-id** binding holds against
  every negative test: wrong nonce, wrong key, mutated bytes,
  replay.

## Subcommands

```
merlin-attest init
merlin-attest pub
merlin-attest quote --prog-id N --prog-tag <hex64> [--ns-id M]
                    [--profile P] [--load-time-ns T]
                    --nonce <hex64> -o <out.bin>
merlin-attest verify --pub <pub.pem> --nonce <hex64>
                     [--last-seq N] <quote.bin>
merlin-attest dump <quote.bin>
```

The MAK directory defaults to `/tmp/merlin-mak` (override with
`MERLIN_MAK_DIR=...`).  Three files live there:

```
mak.priv.pem    ed25519 private key (0600)
mak.pub.pem     ed25519 public key  (0644)
mak.seq         monotonic counter (per-MAK)
```

In the eventual kernel implementation the MAK lives in the kernel
keyring (optionally TPM-sealed) and `mak.seq` becomes a per-MAK
atomic counter; the algorithm is identical to what this prototype
demonstrates.

## Build

```bash
sudo apt install libssl-dev
make
make test    # 10-case battery
```

## Test battery (10/10 pass)

```
[PASS] init creates MAK keys + pub-hash
[PASS] init idempotent
[PASS] round-trip: sign + verify with matching nonce
[PASS] verify rejects wrong nonce
[PASS] quote_seq strictly increases
[PASS] verify rejects when last-seq >= quote.seq (replay defence)
[PASS] verify accepts when last-seq < quote.seq
[PASS] verify rejects wrong public key
[PASS] verify rejects content mutation
[PASS] dump prints algo + prog_tag
```

## End-to-end flow

A controller deploying program X to host H runs through this dance:

```
1. controller        host                                  controller
   pick prog_tag     (host has program X loaded by some
                     earlier flow; prog_tag was computed
                     by `merlin-sign tag` against X.o)
2. send NONCE  -->  ...
3.                   merlin-attest quote                       (kernel-side
                       --prog-id N --prog-tag <X tag>           equivalent of
                       --nonce <NONCE>  -o quote.bin             this in-kernel
4. recv quote  <--                                            attestation cmd)
5. merlin-attest verify --pub <hostMAK.pub> --nonce <NONCE>
                         --last-seq <previous>  quote.bin
   exit 0 == proof H really loaded the program tagged X.
```

The host-side step 3 is exactly what `MERLIN_PROG_GET_ATTESTATION`
(a `merlin_cmd` reserved in `09-mvcp-kernel-uapi.md` §4) will do
when the kernel loader lands.

## What's not yet implemented

- ECDSA-P256 / RSA-PSS-2048 algorithm support (v0 = ed25519 only).
- HW chain (TPM 2.0 / CCA / TDX / SEV-SNP / Keystone / RISC-V AIA).
  The struct fields are present and zeroed; the producer leaves
  `hw_chain_present = 0`.
- Constant-time nonce / signature comparison.  v0 uses `memcmp`;
  this is fine for the prototype but the kernel-side verifier
  will use `crypto_memneq`-style primitives.
- MAK sealing to TPM NV index (kernel concern; `proto-kernel-loader`).
- Cross-platform MAK rotation (the design's `MERLIN_KEYRING_ROTATE_MAK`
  command).

## Relationship to other tools

```
.merlin.o
   |   merlin-objtool sha256          -> 32-byte hash
   |   merlin-sign tag                -> same 32-byte hash (verified)
   v
prog_tag (32 bytes)
   |
   v
merlin-attest quote --prog-tag <prog_tag> --nonce <N>  -o quote.bin
   |                                                       (host)
   |   signed by MAK
   v
quote.bin (240 + 64 bytes; ed25519 in v0)
   |
   v
merlin-attest verify --pub <MAK.pub.pem> --nonce <N>
                     --last-seq <last>  quote.bin            (controller)
```

The `prog_tag` carried by the quote is the same byte-string the
signing tool produces.  Cross-tool tag agreement (proved by the
`merlin-sign` test battery) means the attestation in turn
cryptographically commits to a *specific* signed program.
