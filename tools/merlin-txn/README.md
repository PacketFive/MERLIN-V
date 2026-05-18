# `merlin-txn` — prototype MVCP batch-transaction engine

The eighth user-space code prototype of MERLIN-V.  Implements
**MVCP atomic map-batch transactions** per
[`../../docs/design/09-mvcp-kernel-uapi.md`](../../docs/design/09-mvcp-kernel-uapi.md) §3.3.

## What the prototype proves

- The canonical op and stats structs (`merlin_txn_op_v1`,
  `merlin_txn_stats_v1`) are pinned in
  [`../../docs/design/uapi/merlin/txn.h`](../../docs/design/uapi/merlin/txn.h).
- All five op types work correctly: INSERT (fail-if-exists),
  UPDATE (fail-if-absent), UPSERT, DELETE (idempotent), REPLACE_ALL.
- Atomic-or-none semantics: a commit either applies all staged
  ops or aborts.
- Abort before commit leaves maps untouched.
- `MERLIN_TXN_F_DRAIN_RCU` vs `MERLIN_TXN_F_FAST` flag distinction
  is recorded and observable in the commit output.
- The 4096-op limit (`MERLIN_TXN_MAX_OPS`) is enforced.
- Commit stats (`ops_staged`, `ops_applied`, `ops_skipped`) are
  accurate: absent-key DELETE counts as skipped, not error.
- Sequential commits to the same map are correctly serialised.
- Disjoint-set commits to different maps do not interfere.

## Subcommands / script language

The tool accepts either a `script <file.txn>` or a single command
inline.  Script commands map 1:1 to kernel UAPI:

```
map-create               -> handle N
map-destroy N
map-dump N
map-lookup N key

txn-begin [ns_id]
txn-stage map_handle op key [value]
    op: insert | update | upsert | delete | replace-all
txn-commit [drain|fast]
txn-abort
```

The script format makes tests read as annotated call traces —
easy to map to the eventual kernel `merlin(2)` calls.

## Build & test

```bash
make
make test    # 12-case battery; no external deps
```

## Test battery (12/12 pass)

```
[PASS] single INSERT + lookup
[PASS] UPSERT overwrites existing key
[PASS] DELETE idempotent (applied=1 skipped=1, key gone)
[PASS] multi-map: both maps updated atomically
[PASS] abort leaves map untouched
[PASS] INSERT on existing key skipped, old value preserved
[PASS] TXN_F_FAST skips drain (drain=no)
[PASS] TXN_F_DRAIN_RCU recorded (drain=yes)
[PASS] staged-ops overflow rejected (> MERLIN_TXN_MAX_OPS=4096)
[PASS] sequential commits to same map: second wins
[PASS] disjoint commits independent
[PASS] commit stats: staged=4 applied=3 skipped=1
```

## Semantics

**Atomic-or-none across all touched maps.** A commit applies every
staged op in sequence.  A hard error (ENOSPC, EBADF) aborts and
the transaction rolls back.  Soft errors (INSERT on existing key →
EEXIST, DELETE of absent key → ENOENT) count as `ops_skipped` but
do not abort the commit.

**Conflict model (simplified for user space).** In the kernel,
concurrent commits on overlapping map sets are serialised on a
per-namespace transaction lock.  The prototype models this with a
per-map `g_map_locked[]` flag: a commit locks every touched map,
applies ops, then unlocks.  A second concurrent commit (impossible
in the single-threaded prototype but possible in the kernel) would
see `EBUSY` (`-ECONFLICT` in the kernel).  Sequential commits are
always safe because unlock completes before the next commit begins.

**RCU drain (simulated).** `MERLIN_TXN_F_DRAIN_RCU` is the default.
In the kernel this waits for an RCU grace period so no in-flight
program is still reading the pre-commit state on return.  In the
prototype the flag is recorded (`drain=yes` in output) but no actual
synchronisation is needed (single-threaded, no concurrency).
`MERLIN_TXN_F_FAST` sets `drain=no`; the caller owns quiesce.

## What's not yet implemented

- Conflict detection between truly concurrent goroutines/threads
  (user-space prototype is single-threaded; kernel uses an RCU
  per-namespace spinlock).
- REPLACE_ALL with a full replacement array (the prototype accepts
  only a single (key, value) pair for simplicity).
- Per-namespace quota enforcement on batch staging (max ops is
  global at `MERLIN_TXN_MAX_OPS`; the kernel's sysctl-tunable
  per-namespace limit is future work).
- Timeout enforcement on open transactions (`timeout_ns` in
  `MERLIN_MAP_BATCH_TXN_BEGIN`); the prototype ignores it.

## Relationship to other tools

The batch-transaction engine is the last pure-user-space MVCP
layer-A prototype.  The full user-space pipeline now covers:

```
.merlin.o  ->  objtool  ->  sign / verify
           ->  verifier ->  jit  ->  shim  ->  telemetry stats
           ->  attest quote / verify
           ->  ns scope-check
           ->  txn: stage ops, commit atomically
```

The next remaining layer-A item is `proto-mvcp-multicast`
(MERLIN_NL_CTRL netlink), which has material kernel dependencies
and is deferred until `proto-kernel-loader` lands.
