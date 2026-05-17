# `merlin-telemetry` — prototype telemetry shim

The fifth user-space code prototype of MERLIN-V.  Implements the
**MVCP standard telemetry export** per
[`../../docs/design/09-mvcp-kernel-uapi.md`](../../docs/design/09-mvcp-kernel-uapi.md) §3.5
— the canonical per-program counter block emitted automatically by
the dispatch shim around every program invocation.

This is the user-space analogue of what
`kernel/merlin/dispatch.c` will do in the eventual kernel
implementation: wrap every JIT'd program with a shim that
measures `CLOCK_MONOTONIC` ns around the call and updates the
counters.

## What this proves

- The canonical counter struct `merlin_prog_stats_v1` is now
  pinned in a real header
  ([`../../docs/design/uapi/merlin/stats.h`](../../docs/design/uapi/merlin/stats.h)).
- The shim's instrumentation overhead is exactly "two
  `clock_gettime` calls plus three integer additions per
  invocation" — no instructions in the JIT'd program itself.
- The wire format round-trips: write stats to a binary file,
  read back, parse with forward-compat tolerance, render as text.

## Subcommands

```
merlin-telemetry run  -n <iters> <object.o>
merlin-telemetry dump <stats.bin>
merlin-telemetry text <stats.bin>
```

The `run` subcommand uses the
[`../merlin-jit-x86_64/`](../merlin-jit-x86_64/) prototype to JIT
the program, then wraps it in the dispatch shim and invokes it
`<iters>` times.  Output is the canonical stats block in tracefs
text format plus the last invocation's return value.

The `dump` / `text` subcommands read a wire-format
`merlin_prog_stats_v1` from a binary file (the same format kernel
tracefs binary will emit and netlink-multicast `_TELEMETRY_SAMPLE`
events will carry).  They demonstrate forward-compat parsing:
consult `size`, copy `min(size, sizeof(local))` bytes, ignore the
tail.  The test battery includes a "future kernel" fixture (200
bytes advertised) to prove this works.

## Build

```bash
sudo apt install libelf-dev
make
make test    # 6-case battery
```

## Test battery (6/6 pass)

```
[PASS] drop run_count=7 verdict[1]=7
[PASS] two_helpers run_count=5 helper_call_count=10
[PASS] run_ns_total grows (... ns @ 1 iter, ... ns @ 100 iters)
[PASS] dump parses wire format
[PASS] text format labels verdicts (DROP, PASS, etc.)
[PASS] forward-compat: future kernel size accepted; known fields read
```

The forward-compat case is the most important: it asserts the
spec's commitment that *future* kernel versions can grow
`merlin_prog_stats_v1` by appending fields (and bumping `size`)
without breaking older readers.

## Wire format

```
offset  size  field
0       4     size            sizeof(struct) as written
4       4     _pad            MBZ
8       8     run_count
16      8     run_ns_total
24      64    verdict_count[8]
88      8     helper_call_count
96      8     helper_fault_count
104     8     verifier_load_ns
112     8     last_run_time_ns
120     32    _reserved[4]
total: 152 bytes for v1
```

(The struct is 152 bytes via natural alignment; future v1.x
extensions append in the reserved or beyond.)

## Three access paths (spec §3.5)

| Path | This prototype | Kernel implementation |
| ---- | -------------- | --------------------- |
| tracefs text | `merlin-telemetry text` | per-program directory under `/sys/kernel/tracing/merlin/<tag>/stats` |
| tracefs binary | `merlin-telemetry dump` against `stats_raw` | same dir, `stats_raw` file |
| syscall | (will use shim's snapshot) | tail of `struct merlin_prog_info` returned by `MERLIN_PROG_GET_INFO_BY_FD` |
| netlink-multicast | (snapshot serialised) | `MERLIN_NL_CTRL` `_TELEMETRY_SAMPLE` events |

## Relationship to other tools

```
.merlin.o
   |
   v
merlin-objtool / merlin-verifier / merlin-jit-x86_64
   |
   |   jit returns a merlin_jit_fn
   v
merlin_prog_init(fn) -> merlin_prog_invoke() emits stats
   |
   |   merlin_prog_snapshot()
   v
struct merlin_prog_stats_v1 wire format
   |
   +-> tracefs binary    (proto: file)
   +-> tracefs text      (proto: `merlin-telemetry text`)
   +-> netlink multicast (future: proto-mvcp-multicast)
   +-> syscall info      (future: proto-kernel-loader)
```

## What's not yet implemented

- Per-CPU counter sharding (the eventual kernel implementation
  uses per-CPU counters folded at read; v0 prototype uses one
  shared counter per program because we're single-threaded).
- Netlink-multicast streaming (`proto-mvcp-multicast`).
- tracefs filesystem integration (kernel-side concern;
  `proto-kernel-loader`).
- Configurable streaming interval.
- Cumulative-overflow detection (the spec commits 64-bit
  counters never overflow practically, but exporters need
  defined wrap semantics).
