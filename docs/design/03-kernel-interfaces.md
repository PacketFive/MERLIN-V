# 03 — Kernel Interfaces

Status: starter
Owner: PacketFive
Last reviewed: initial draft

This document defines how Linux user space and the rest of the kernel
talk to BPF-V. It is the source of truth for the eventual UAPI patch
in the RFC series.

References:
- `bpf-next/include/uapi/linux/bpf.h` (eBPF UAPI, reference shape).
- `bpf-next/kernel/bpf/syscall.c` (eBPF syscall dispatcher).
- `bpf-next/Documentation/bpf/` (overall structure).
- Linux kernel UAPI rules: `Documentation/process/adding-syscalls.rst`.

## 1. Where BPF-V lives in the tree

Proposed in-tree layout (against `bpf-next`):

```
kernel/bpfv/
  Makefile
  Kconfig
  syscall.c          # BPFV() syscall multiplexer
  core.c             # program object lifecycle
  loader.c           # ELF parsing, section validation, relocations
  verifier.c         # BPF-V verifier (calls into shared infra)
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
  linux/bpfv.h           # in-kernel API
  uapi/linux/bpfv.h      # UAPI
Documentation/bpfv/
  index.rst
  instruction-set.rst    # mirrors docs/design/02-*
  syscall.rst            # mirrors this document
tools/
  lib/bpfv/              # libbpfv (BSD-2-Clause / GPL-2.0 dual)
  bpfv/
    bpfvtool/            # CLI: load, dump, verify, prog list, map dump
  testing/selftests/bpfv/
```

The `kernel/bpfv/` directory is *additive*; it does not modify
`kernel/bpf/`. Where the two share infrastructure (maps, BTF, link
machinery), BPF-V calls into existing BPF symbols rather than
duplicating them. The split between *shared* and *parallel*
infrastructure is itself an open question — see §6.

## 2. New syscall

A single multiplexer, modelled on `bpf(2)`:

```c
SYSCALL_DEFINE3(bpfv,
                int, cmd,
                union bpfv_attr __user *, uattr,
                unsigned int, size);
```

Initial command set:

| cmd | Purpose |
| --- | ------- |
| `BPFV_PROG_LOAD` | Submit an ELF blob; receive a prog fd. |
| `BPFV_PROG_TEST_RUN` | Execute a loaded program against a user-supplied input buffer. |
| `BPFV_PROG_GET_INFO_BY_FD` | Introspection. |
| `BPFV_MAP_CREATE` | Same semantics as `BPF_MAP_CREATE`. |
| `BPFV_MAP_*_ELEM` | Lookup / update / delete / get-next. |
| `BPFV_LINK_CREATE` | Attach a program to a hook. |
| `BPFV_LINK_UPDATE` | Replace a program at an existing link. |
| `BPFV_BTF_LOAD` | Submit BTF-V. |
| `BPFV_OBJ_PIN` / `BPFV_OBJ_GET` | bpffs-style pinning (parallel mount: `bpfvfs`, TBD). |

The `union bpfv_attr` mirrors `union bpf_attr` field-for-field where it
makes sense (so map ops can be near-identical), and adds:

```c
struct {
    __u64 elf_ptr;        // user pointer to ELF blob
    __u32 elf_len;
    __u32 prog_type;      // BPFV_PROG_TYPE_*
    __u32 expected_attach_type;
    __u32 profile;        // BPFV_PROFILE_* (rv32imc-ilp32, rv64imac-lp64, ...)
    __u32 log_level;
    __u64 log_buf;
    __u32 log_size;
    char  prog_name[BPFV_OBJ_NAME_LEN];
    __u64 license_ptr;    // user pointer to NUL-terminated license string
    /* ... */
} prog_load;
```

`prog_type` enumerates BPF-V hook contracts. Initial set:

- `BPFV_PROG_TYPE_XDP_V`
- `BPFV_PROG_TYPE_TC_V`
- `BPFV_PROG_TYPE_KPROBE_V`
- `BPFV_PROG_TYPE_TRACEPOINT_V`
- `BPFV_PROG_TYPE_SOCKET_FILTER_V`
- `BPFV_PROG_TYPE_PERF_EVENT_V`

Each corresponds to a small in-kernel record describing its `ctx` type,
allowed helpers, and attach machinery. See [04-toolchain.md](04-toolchain.md)
§BTF-V for how `ctx` types are declared.

## 3. Capability and namespacing

- A new bit, **`CAP_BPFV`**, gates *all* BPFV commands by default. On
  systems without `CAP_BPFV`, BPF-V is unavailable.
- Unprivileged BPF-V is **off by default**, controlled by
  `kernel.unprivileged_bpfv_disabled` (default `2` = hard-disabled, mirrors
  the eBPF tightening trajectory). It is acceptable to launch the
  feature with no unprivileged mode at all.
- Network namespaces, cgroup-v2, and user namespaces all gate hook
  attachment (e.g. XDP-V attach requires `CAP_NET_ADMIN` in the netns).

## 4. Hook surfaces

BPF-V program types attach via the same kernel hook surfaces eBPF uses
(XDP, TC ingress/egress, kprobes, tracepoints, socket filters,
perf-event handlers). The mechanism is:

1. Hook calls a single entrypoint, `bpfv_dispatch(prog, ctx)`.
2. `bpfv_dispatch` invokes the compiled image (pass-through or JITed).
3. The image runs to completion; return code drives the hook.

For cohabitation with eBPF on the same hook, the kernel maintains
*parallel* run-lists: eBPF programs run, then BPF-V programs run, with
verdict combination identical to multi-prog eBPF semantics. This is
explicitly a Phase-2 nicety; Phase 1 supports only one or the other on
a given attach point.

## 5. Maps

Maps are reused from `kernel/bpf/`. BPF-V's `BPFV_MAP_*` commands
delegate to the corresponding `bpf_map_*` paths. Justification:

- Map types, locking, RCU discipline, and memory accounting are
  battle-tested.
- libbpfv can use existing tooling for map dumps and pinning.
- The verifier still treats map-value pointers as a BPF-V type; only
  the *implementation* is shared.

If a BPF-V-only map type is ever needed (e.g. a RISC-V-specific
hardware-offloaded map), it is registered through the same
`struct bpf_map_ops` registry with a flag bit.

## 6. Open architecture questions

- **Verifier sharing.** Should `kernel/bpfv/verifier.c` call into
  `kernel/bpf/verifier.c` for type-state plumbing (a refactor to expose
  the abstract domain), or stay independent? See
  [06-verifier.md](06-verifier.md).
- **BTF sharing.** BTF-V is conceptually BTF with extra reloc kinds.
  Same `.BTF` section format with new tag space? Or a parallel
  `.BTF.v`? Decision affects pahole / libbpf changes.
- **fs.** Pin BPF-V objects under existing `bpffs` (`/sys/fs/bpf/`) or
  a new `bpfvfs`? Strong preference for reusing `bpffs` with object-
  type discrimination.
- **Unprivileged path.** Whether to ever have one. eBPF's unprivileged
  surface has been almost entirely retracted; BPF-V should likely
  launch privileged-only and treat unprivileged as a research question.
- **uapi naming prefix.** `BPFV_` vs `BPF_V_` — going with `BPFV_` for
  brevity and to avoid clashing visually with `BPF_VERSION`/etc.

## 7. UAPI stability promise

Once a `BPFV_*` UAPI element is shipped in a released kernel it is
**permanent and additive-only**, per Linux UAPI policy. This means the
RFC must lock down `struct bpfv_attr`, command numbers, and program
types carefully before merge. The current document is *not* yet that
spec — it is the staging area for it.
