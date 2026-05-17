# `<merlin/*.h>` - program-side headers

This directory holds the draft headers a **MERLIN-V program**
includes when written in C.  They are the user-side counterpart
to the kernel UAPI in [`../linux/`](../linux/):

| Where | Who includes it | What it describes |
| ----- | ---------------- | ---------------- |
| `linux/merlin.h`, `linux/if_mvdp.h` | userspace loaders, kernel | kernel UAPI: syscall, sockopts, info structs |
| `merlin/*.h`                         | MERLIN-V programs        | program-side: helpers, maps, sections, BTF |

Eventual in-tree paths:

| Draft path | In-tree path |
| ---------- | ------------ |
| `merlin/types.h`           | `tools/lib/merlin/include/merlin/types.h` (and `usr/include` install) |
| `merlin/helpers.h`         | `tools/lib/merlin/include/merlin/helpers.h` |
| `merlin/maps.h`            | `tools/lib/merlin/include/merlin/maps.h` |
| `merlin/section_macros.h`  | `tools/lib/merlin/include/merlin/section_macros.h` |
| `merlin/license.h`         | `tools/lib/merlin/include/merlin/license.h` |
| `merlin/core.h`            | `tools/lib/merlin/include/merlin/core.h` |
| `merlin/stats.h`           | `tools/lib/merlin/include/merlin/stats.h` |
| `merlin/attestation.h`     | `tools/lib/merlin/include/merlin/attestation.h` |
| `merlin/mvdp.h`            | `tools/lib/merlin/include/merlin/mvdp.h` |
| `merlin/merlin.h`          | `tools/lib/merlin/include/merlin/merlin.h` (umbrella) |

The layout mirrors `tools/lib/bpf/`'s shipping of `<bpf/*.h>` to
user-space programs.

## Inclusion contract

A typical MERLIN-V program needs:

```c
#include <merlin/merlin.h>      /* umbrella: helpers, maps, sections, etc.  */
#include <merlin/mvdp.h>        /* if it's an MVDP program                  */
/* OR <merlin/xdp_v.h>, <merlin/tc_v.h>, <merlin/kprobe.h>, etc.            */
```

Programs in other languages bind to these same headers via FFI
(initially just C and Rust; see `libmerlin` plans in
[`../../04-toolchain.md`](../../04-toolchain.md) §2).

## Header strategy: GCC and Clang share

**Decided.** One header set covers both compilers.  Where Clang
has a built-in (`__builtin_preserve_access_index` etc.) and GCC
does not, the header uses `#ifdef __clang__` to switch
implementations; the source-level interface stays identical.

The GCC side may rely on a wrapper macro or a section-marker
attribute that the objtool pipeline scans.  This pipeline is
documented in [`../../04-toolchain.md`](../../04-toolchain.md) §4
and the placeholder shipped here will harden as
`design-corev-spec` lands.

## Helper ID allocation

Helper IDs are UAPI - once a helper is published with an ID, that
ID is frozen forever.  The allocation ranges are:

| Range | Purpose |
| ----- | ------- |
| `0x0000 .. 0x00FF` | reserved (verifier rejects)                          |
| `0x0100 .. 0x01FF` | common helpers (any program type)                    |
| `0x0200 .. 0x02FF` | MVDP / network data path helpers                     |
| `0x0300 .. 0x03FF` | tracing helpers (kprobe / tracepoint / perf)         |
| `0x0400 .. 0x04FF` | socket-filter helpers                                |
| `0x0500 .. 0x07FF` | reserved for future MERLIN-V program types           |
| `0x0800 .. 0x0FFF` | reserved for vendor / accelerator helpers            |
| `>= 0x1000`        | illegal (does not fit 12-bit immediate; the loader   |
|                    | rewrite from "li a7, ID; ecall" assumes 8 bytes)     |

The full per-ID list is in [`helpers.h`](helpers.h).  Reserving an
ID is the kernel-side commitment to never re-use it.

## Source-of-truth ordering

Across both UAPI directories the rule from
[`../README.md`](../README.md) applies: the `.h` files in this
tree are the canonical truth for layouts and constants.  When a
`.md` design document disagrees with a `.h`, the `.h` wins and the
`.md` is the bug.

## Stability

DRAFT.  Anything in this directory may change before the RFC v1
patch series is posted.  Once posted, the layouts and helper IDs
are frozen by Linux UAPI rules.
