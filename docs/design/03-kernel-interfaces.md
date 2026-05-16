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

Initial command set:

| cmd | Purpose |
| --- | ------- |
| `MERLIN_PROG_LOAD` | Submit an ELF blob; receive a prog fd. |
| `MERLIN_PROG_TEST_RUN` | Execute a loaded program against a user-supplied input buffer. |
| `MERLIN_PROG_GET_INFO_BY_FD` | Introspection. |
| `MERLIN_MAP_CREATE` | Same semantics as `BPF_MAP_CREATE`. |
| `MERLIN_MAP_*_ELEM` | Lookup / update / delete / get-next. |
| `MERLIN_LINK_CREATE` | Attach a program to a hook. |
| `MERLIN_LINK_UPDATE` | Replace a program at an existing link. |
| `MERLIN_BTF_LOAD` | Submit MERLIN BTF. |
| `MERLIN_OBJ_PIN` / `MERLIN_OBJ_GET` | bpffs-style pinning (parallel mount: `merlinfs`, TBD). |

The `union merlin_attr` mirrors `union bpf_attr` field-for-field where it
makes sense (so map ops can be near-identical), and adds:

```c
struct {
    __u64 elf_ptr;        // user pointer to ELF blob
    __u32 elf_len;
    __u32 prog_type;      // MERLIN_PROG_TYPE_*
    __u32 expected_attach_type;
    __u32 profile;        // MERLIN_PROFILE_LINUX_RV64 | MERLIN_PROFILE_RTOS_RV32
    __u32 log_level;
    __u64 log_buf;
    __u32 log_size;
    char  prog_name[MERLIN_OBJ_NAME_LEN];
    __u64 license_ptr;    // user pointer to NUL-terminated license string
    /* ... */
} prog_load;
```

The `profile` field is the *bytecode* profile declared in the
program's `.merlin.meta` section. Initial values:

| Value | Bytecode profile | march string |
| ----- | ---------------- | ------------ |
| `MERLIN_PROFILE_LINUX_RV64` | `merlin-linux-rv64` | `rv64imac_zicsr_zifencei` |
| `MERLIN_PROFILE_RTOS_RV32`  | `merlin-rtos-rv32`  | `rv32imc_zicsr_zifencei` (optional `_a`) |

See [02-isa-and-bytecode.md](02-isa-and-bytecode.md) §1 for
profile definitions and §3 for per-profile extension policy.
The *verifier* profile (default / sleepable / largemem / ...) is
selected separately based on the hook the program will attach
to; see [06-verifier.md](06-verifier.md) §7.

`prog_type` enumerates MERLIN-V hook contracts. Initial set:

- `MERLIN_PROG_TYPE_XDP_V`
- `MERLIN_PROG_TYPE_TC_V`
- `MERLIN_PROG_TYPE_KPROBE_V`
- `MERLIN_PROG_TYPE_TRACEPOINT_V`
- `MERLIN_PROG_TYPE_SOCKET_FILTER_V`
- `MERLIN_PROG_TYPE_PERF_EVENT_V`

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
