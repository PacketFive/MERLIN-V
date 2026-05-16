# Lab 08 — Out-of-tree Kernel Module

Module: 5
Effort tier: L
Prerequisites: Lab 07.
Design doc: [`docs/design/03-kernel-interfaces.md`](../design/03-kernel-interfaces.md).

## Learning objectives

- Build, load, and unload an out-of-tree Linux kernel module against
  the kernel built from this repo's `bpf-next` submodule.
- Implement a misc character device (`miscdevice`) with `ioctl(2)`
  command set.
- Allocate executable kernel pages safely.
- Flush kernel-side I-cache and reason about SMP shootdown.
- Reuse, in-kernel, the user-space verifier and pass-through JIT from
  prior labs (linked statically into the module).

## Background reading

- *Linux Device Drivers, 3rd ed.* (LDD3), chapters on misc devices
  and ioctls. (LDD3 is dated but the concepts are intact.)
- `bpf-next/Documentation/process/coding-style.rst`.
- `bpf-next/kernel/bpf/core.c` — see `bpf_jit_binary_alloc()` and
  friends; pattern your allocation after this.
- `bpf-next/arch/riscv/include/asm/cacheflush.h`.

## Specification

You will ship `kmod/merlin.ko` exposing `/dev/merlin` with the following
`ioctl`s:

```c
#define MERLIN_IOC_MAGIC  'M'

struct merlin_load_attr {
    __u64 elf_ptr;   // user pointer
    __u32 elf_size;
    __u32 flags;
    char  name[32];
};

struct merlin_run_attr {
    __u32 prog_id;
    __u64 ctx_ptr;
    __u32 ctx_size;
    __s32 retval;    // out
};

#define MERLIN_IOC_LOAD    _IOWR(MERLIN_IOC_MAGIC, 0x01, struct merlin_load_attr)
#define MERLIN_IOC_RUN     _IOWR(MERLIN_IOC_MAGIC, 0x02, struct merlin_run_attr)
#define MERLIN_IOC_UNLOAD  _IOW (MERLIN_IOC_MAGIC, 0x03, __u32)
#define MERLIN_IOC_INFO    _IOR (MERLIN_IOC_MAGIC, 0x04, struct merlin_info)
```

### Module-side flow

1. `LOAD`:
   - Copy ELF from user space (`copy_from_user`).
   - Validate (Lab 05 logic compiled into the module).
   - Verify (Lab 04 logic compiled into the module).
   - Allocate kernel exec pages (see §"Allocation").
   - Copy and relocate.
   - Flush I-cache.
   - Return a `prog_id` (a small monotonic integer).
2. `RUN`:
   - Copy the user `ctx` buffer into a per-call kernel scratch area.
   - Call the program with the kernel-pointer ctx.
   - Copy any updated bytes back.
   - Return `retval`.
3. `UNLOAD`:
   - Free pages, drop the entry from the in-module table.

### Allocation

Use `__vmalloc_node_range(...)` with `PAGE_KERNEL_EXEC` (or
`bpf_prog_pack` mechanism for shared exec pools on newer kernels —
your choice; document which in `WRITEUP.md`).

On RISC-V hosts the module hosts the *pass-through* path: copy the
verified `.text` verbatim. On non-RISC-V hosts, the module hosts the
*host JIT* path: pull in Lab 07's translator.

### Safety constraints

- Preemption must be disabled across the program execution
  (`preempt_disable()` / `preempt_enable()`).
- Program runs with kernel privileges; the verifier is the only
  defence.
- No `kmalloc(GFP_KERNEL)` inside the program path (programs cannot
  sleep). All allocation is at LOAD time.

## Tasks

### Task 1 — Skeleton module

Build a minimal `merlin.ko` that registers a `miscdevice` and prints
to `dmesg` on open/close. Load and unload it repeatedly. Confirm with
`/sys/module/merlin/` and `lsmod`.

### Task 2 — Static-link prior labs

Convert Lab 04 (verifier) and Lab 05 (validator) source into a small
in-kernel library `kmod/lib/`. Adjust `printf` → `pr_info`, `malloc`
→ `kmalloc/kvmalloc`, etc. The lab skeleton ships a porting checklist.

### Task 3 — `LOAD`

Implement the ioctl. Test with a user-space program that opens
`/dev/merlin` and submits a good ELF and a bad ELF.

### Task 4 — Exec pages

Implement allocator wrappers. Confirm with `/proc/vmallocinfo` that
the pages are mapped executable.

### Task 5 — `RUN`

Implement program execution. On RISC-V: pass-through. On x86\_64:
host JIT.

### Task 6 — Bad-program stress test

Provide and run a small fuzz harness that feeds malformed ELFs and
deliberately verifier-rejected programs. The module must **never**
panic or oops. The grader runs this and watches `dmesg` for taints
and BUGs.

### Task 7 — Concurrency

Run two threads, each calling `RUN` continuously on the same loaded
program with different `ctx` buffers, for one minute. The grader
checks the kernel log for warnings and confirms results are
consistent.

## Deliverables

- `kmod/` source tree.
- A user-space test driver under `user/`.
- A passing autograder log.
- `WRITEUP.md`:
  - Where did you place the verifier code in the module, and why
    there?
  - How did you handle the absence of `errno` in kernel space?
  - What kept `RUN` reentrant?
  - One issue the kernel build caught that the user-space build
    didn't.

## Rubric

| Criterion | Points |
| --------- | ------ |
| Module builds, loads, unloads without warnings | 10 |
| `LOAD` validates and verifies correctly | 20 |
| Exec pages allocated and protected correctly | 15 |
| `RUN` works for at least three programs from prior labs | 20 |
| Fuzz stress test causes no panics | 20 |
| Concurrency stress test passes | 5 |
| Writeup quality and AI attribution | 10 |
| **Total** | **100** |

## Common pitfalls

- Building against the wrong kernel headers. The module *must* be
  built against the kernel you booted (the `bpf-next` submodule
  build).
- Forgetting `MODULE_LICENSE("GPL")`. Module load will succeed but
  GPL-only symbols won't link.
- `copy_from_user` returning bytes-not-copied, not -EFAULT.
- I-cache not flushed → spurious crashes on second load.

## What's next

Lab 09 brings the same runtime to Zephyr on `qemu_riscv32`, exposing
the differences between Linux and an RTOS environment.
