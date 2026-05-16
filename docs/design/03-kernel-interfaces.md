# 03 — Kernel Interfaces

Status: starter
Owner: PacketFive
Last reviewed: initial draft

This document defines how Linux user space and the rest of the kernel
talk to MERLIN-V. It is the source of truth for the eventual UAPI patch
in the RFC series.

References:
- `bpf-next/include/uapi/linux/bpf.h` (eBPF UAPI, reference shape).
- `bpf-next/kernel/bpf/syscall.c` (eBPF syscall dispatcher).
- `bpf-next/Documentation/bpf/` (overall structure).
- Linux kernel UAPI rules: `Documentation/process/adding-syscalls.rst`.

## 1. Where MERLIN-V lives in the tree

Proposed in-tree layout (against `bpf-next`):

```
kernel/merlin/
  Makefile
  Kconfig
  syscall.c          # MERLIN() syscall multiplexer
  core.c             # program object lifecycle
  loader.c           # ELF parsing, section validation, relocations
  verifier.c         # MERLIN-V verifier (calls into shared infra)
  dispatch.c         # helper / kfunc dispatch glue
  maps.c             # map ops thin wrappers (mostly delegate to BPF maps)
  jit/
    pass_through.c   # RISC-V host: validate + relocate
    host_jit.c       # non-RISC-V host: translate to host ISA
    arch/
      x86_64.c
      arm64.c
      ...
include/
  linux/merlin.h           # in-kernel API
  uapi/linux/merlin.h      # UAPI
Documentation/merlin/
  index.rst
  instruction-set.rst    # mirrors docs/design/02-*
  syscall.rst            # mirrors this document
tools/
  lib/merlin/              # libmerlin (BSD-2-Clause / GPL-2.0 dual)
  merlin/
    merlintool/            # CLI: load, dump, verify, prog list, map dump
  testing/selftests/merlin/
```

The `kernel/merlin/` directory is *additive*; it does not modify
`kernel/bpf/`. Where the two share infrastructure (maps, BTF, link
machinery), MERLIN-V calls into existing BPF symbols rather than
duplicating them. The split between *shared* and *parallel*
infrastructure is itself an open question — see §6.

## 2. New syscall

A single multiplexer, modelled on `bpf(2)`:

```c
SYSCALL_DEFINE3(merlin,
                int, cmd,
                union merlin_attr __user *, uattr,
                unsigned int, size);
```

### 2.1 Canonical UAPI

The wire format of `union merlin_attr`, command numbers (`enum
merlin_cmd`), program / map / attach / profile enumerations, and
prog-load / map-create / map-update flag bits are specified by the
draft header

- [`uapi/linux/merlin.h`](uapi/linux/merlin.h)

with the MVDP / AF\_MVDP socket-family UAPI in

- [`uapi/linux/if_mvdp.h`](uapi/linux/if_mvdp.h)

These headers are the **source of truth** for layouts and constants
(per [`uapi/README.md`](uapi/README.md)). The rest of this section
is rationale and the summary tables; if a value here disagrees with
the header, the header wins.

### 2.2 Initial command set

| `cmd` | Purpose |
| ----- | ------- |
| `MERLIN_PROG_LOAD` | Submit an ELF blob; receive a prog fd. |
| `MERLIN_PROG_TEST_RUN` | Execute a loaded program against a user-supplied input buffer. |
| `MERLIN_PROG_GET_INFO_BY_FD` | Introspection (`struct merlin_prog_info`). |
| `MERLIN_PROG_GET_NEXT_ID` / `MERLIN_PROG_GET_FD_BY_ID` | Iteration and lookup by ID. |
| `MERLIN_MAP_CREATE` | Create a map (delegates to `kernel/bpf/` map ops). |
| `MERLIN_MAP_{LOOKUP,UPDATE,DELETE,GET_NEXT}_ELEM` | Element operations. |
| `MERLIN_MAP_GET_INFO_BY_FD` / `MERLIN_MAP_GET_FD_BY_ID` | Introspection and lookup. |
| `MERLIN_LINK_CREATE` | Attach a program to a hook (XDP-V, TC-V, MVDP, kprobe, ...). |
| `MERLIN_LINK_UPDATE` | Atomically replace a program at an existing link. |
| `MERLIN_LINK_DETACH` | Detach the program. |
| `MERLIN_LINK_GET_INFO_BY_FD` | Introspection (`struct merlin_link_info`). |
| `MERLIN_BTF_LOAD` / `MERLIN_BTF_GET_FD_BY_ID` | Submit and look up MERLIN BTF. |
| `MERLIN_OBJ_PIN` / `MERLIN_OBJ_GET` | bpffs-style pinning (parallel mount: `merlinfs`, TBD). |

The full enumeration with stable numbers is in
[`uapi/linux/merlin.h`](uapi/linux/merlin.h) (`enum merlin_cmd`).

### 2.3 Profile

The `profile` field of `prog_load` is the *bytecode* profile declared in
the program's `.merlin.meta` section.

| Value | Bytecode profile | march string |
| ----- | ---------------- | ------------ |
| `MERLIN_PROFILE_LINUX_RV64` | `merlin-linux-rv64` | `rv64imac_zicsr_zifencei` |
| `MERLIN_PROFILE_RTOS_RV32`  | `merlin-rtos-rv32`  | `rv32imc_zicsr_zifencei` (optional `_a`) |

See [02-isa-and-bytecode.md](02-isa-and-bytecode.md) §1 for profile
definitions and §3 for per-profile extension policy. The *verifier*
profile (default / sleepable / largemem / ...) is selected
separately based on the (prog\_type, attach\_type) pair; see
[06-verifier.md](06-verifier.md) §7.

### 2.4 Program types

`prog_type` enumerates MERLIN-V hook contracts. Initial set:

- `MERLIN_PROG_TYPE_XDP_V` — MERLIN-V program on the existing XDP hook
  (cohabitation; reuses kernel `struct xdp_buff`).
- `MERLIN_PROG_TYPE_TC_V`
- `MERLIN_PROG_TYPE_KPROBE_V`
- `MERLIN_PROG_TYPE_TRACEPOINT_V`
- `MERLIN_PROG_TYPE_SOCKET_FILTER_V`
- `MERLIN_PROG_TYPE_PERF_EVENT_V`
- `MERLIN_PROG_TYPE_MVDP` — MERLIN-V *native* network data path.
  Distinct from `XDP_V`: its own `struct mvdp_md` context, its own
  verdict enum, its own hook modes (SKB / DRV / HW), and its own
  socket family (`AF_MVDP`). See
  [08-mvdp-and-af-mvdp.md](08-mvdp-and-af-mvdp.md) §2.

Each corresponds to a small in-kernel record describing its `ctx` type,
allowed helpers, and attach machinery. See [04-toolchain.md](04-toolchain.md)
§MERLIN BTF for how `ctx` types are declared.

## 3. Capability and namespacing

- A new bit, **`CAP_MERLIN`**, gates *all* MERLIN commands by default. On
  systems without `CAP_MERLIN`, MERLIN-V is unavailable.
- Unprivileged MERLIN-V is **off by default**, controlled by
  `kernel.unprivileged_merlin_disabled` (default `2` = hard-disabled, mirrors
  the eBPF tightening trajectory). It is acceptable to launch the
  feature with no unprivileged mode at all.
- Network namespaces, cgroup-v2, and user namespaces all gate hook
  attachment (e.g. XDP-V attach requires `CAP_NET_ADMIN` in the netns).

## 4. Hook surfaces

MERLIN-V program types attach via the same kernel hook surfaces eBPF uses
(XDP, TC ingress/egress, kprobes, tracepoints, socket filters,
perf-event handlers). The mechanism is:

1. Hook calls a single entrypoint, `merlin_dispatch(prog, ctx)`.
2. `merlin_dispatch` invokes the compiled image (pass-through or JITed).
3. The image runs to completion; return code drives the hook.

For cohabitation with eBPF on the same hook, the kernel maintains
*parallel* run-lists: eBPF programs run, then MERLIN-V programs run, with
verdict combination identical to multi-prog eBPF semantics. This is
explicitly a Phase-2 nicety; Phase 1 supports only one or the other on
a given attach point.

## 5. Maps

Maps are reused from `kernel/bpf/`. MERLIN-V's `MERLIN_MAP_*` commands
delegate to the corresponding `bpf_map_*` paths. Justification:

- Map types, locking, RCU discipline, and memory accounting are
  battle-tested.
- libmerlin can use existing tooling for map dumps and pinning.
- The verifier still treats map-value pointers as a MERLIN-V type; only
  the *implementation* is shared.

If a MERLIN-V-only map type is ever needed (e.g. a RISC-V-specific
hardware-offloaded map), it is registered through the same
`struct bpf_map_ops` registry with a flag bit.

## 6. Open architecture questions

- **Verifier sharing.** Should `kernel/merlin/verifier.c` call into
  `kernel/bpf/verifier.c` for type-state plumbing (a refactor to expose
  the abstract domain), or stay independent? See
  [06-verifier.md](06-verifier.md).
- **BTF sharing.** MERLIN BTF is conceptually BTF with extra reloc kinds.
  Same `.BTF` section format with new tag space? Or a parallel
  `.merlin.btf`? Decision affects pahole / libbpf changes.
- **fs.** Pin MERLIN-V objects under existing `bpffs` (`/sys/fs/bpf/`) or
  a new `merlinfs`? Strong preference for reusing `bpffs` with object-
  type discrimination.
- **Unprivileged path.** Whether to ever have one. eBPF's unprivileged
  surface has been almost entirely retracted; MERLIN-V should likely
  launch privileged-only and treat unprivileged as a research question.
- **uapi naming prefix.** `MERLIN_` vs `BPF_V_` — going with `MERLIN_` for
  brevity and to avoid clashing visually with `BPF_VERSION`/etc.

## 7. UAPI stability promise

Once a `MERLIN_*` UAPI element is shipped in a released kernel it is
**permanent and additive-only**, per Linux UAPI policy. This means the
RFC must lock down `struct merlin_attr`, command numbers, and program
types carefully before merge. The current document is *not* yet that
spec — it is the staging area for it.
