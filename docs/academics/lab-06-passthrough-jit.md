# Lab 06 — Pass-through JIT (user space)

Module: 4
Effort tier: M
Prerequisites: Lab 05.
Design doc: [`docs/design/07-jit-and-offload.md`](../design/07-jit-and-offload.md) §2.

> **You must run this lab on a RISC-V host.** Either the MPFS Icicle
> Kit (a project reference platform) or `qemu-system-riscv64` from
> Lab 00. The whole point of pass-through is that the host CPU is
> RISC-V.

## Learning objectives

- Allocate executable memory (`mmap(PROT_READ | PROT_EXEC)`) and
  understand W^X.
- Apply relocations to a `.text` blob before execution.
- Flush the I-cache correctly on RISC-V (`__builtin___clear_cache` /
  `RISCV_FENCE_I`).
- Implement a working "JIT" that contains zero instruction translation.

## Background reading

- `docs/design/07-jit-and-offload.md` §2 (the algorithm you are
  implementing).
- `man mmap(2)`, `man mprotect(2)`.
- `bpf-next/arch/riscv/include/asm/cacheflush.h` and adjacent —
  observe how the kernel does the same thing.

## Specification

You will build `merlin-jit`, a tool that:

1. Loads a validated MERLIN-V ELF (output of Lab 05's `prog validate`).
2. Verifies it (Lab 04's verifier).
3. Allocates an RX page region.
4. Copies `.text` in.
5. Applies relocations.
6. Calls `__builtin___clear_cache(begin, end)` (which on RISC-V
   emits `fence.i` and any required IPI).
7. Casts the entry pointer to `int32_t (*)(void *ctx)` and calls it
   with a test context.
8. Prints the return value.

If invoked with `--bench`, it loops `N` times and reports cycles per
invocation using `rdcycle` or `clock_gettime(CLOCK_MONOTONIC)`.

### W^X discipline

The lab requires the following sequence:

1. `mmap(NULL, len, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0)`
2. Copy and relocate.
3. `mprotect(ptr, len, PROT_READ | PROT_EXEC)`
4. `__builtin___clear_cache(ptr, ptr + len)`

Single-step W^X (write *and* execute at the same time) is forbidden
even temporarily. The autograder verifies this by `seccomp`-trapping
any `mmap`/`mprotect` call that would result in `W|X` simultaneously.

### Relocations implemented in this lab

- `R_MERLIN_HELPER_ID` — patch the I-immediate of `li a7, ?` (the
  helper id is a compile-time-resolved value).
- `R_RISCV_PCREL_HI20` / `R_RISCV_PCREL_LO12_I` /
  `R_RISCV_PCREL_LO12_S` — standard RISC-V PC-relative pairs, in case
  the program references its own data section.

`R_MERLIN_KFUNC_SLOT`, `R_MERLIN_MAP_FD`, and `R_MERLIN_CORE_FIELD` are
stubbed out in this lab (they'll come back in Lab 08).

## Tasks

### Task 1 — Allocator

Implement `merlin_text_alloc(size, **rx_out, **rw_out)` returning two
aliasing views of the same physical pages — one RW, one RX — by
allocating two `mmap`s on a shared `memfd_create(2)` region.

> This dual-mapping trick is how mature JITs avoid the
> write-then-mprotect dance and stay W^X simultaneously. You may
> instead use the single-mapping + mprotect approach above; both
> satisfy the rubric.

### Task 2 — Reloc application

Implement `src/reloc.c`. Each reloc type has a small, isolated handler.
PC-relative pairs are tricky: you must find the matching `LO12_*`
reloc by its `r_addend` referring back to the `HI20` site.

### Task 3 — Run

Implement `src/run.c`:

```c
typedef int32_t (*merlin_entry_fn)(void *ctx);

int merlin_run_jit(const char *elf_path, void *ctx, int32_t *ret_out);
```

### Task 4 — Benchmark

Implement `--bench N`. Report:
- Mean cycles per invocation.
- Stddev.
- The reported value of `mcycle` (or `cycle`) before and after, when
  available in user space.

### Task 5 — Cross-host check

The autograder runs your tool on:
- `qemu-system-riscv64` Linux from Lab 00.
- A non-RISC-V host (your dev box). On non-RISC-V, your tool must
  detect that and refuse with an informative error pointing to Lab 07
  (the host JIT lab).

## Deliverables

- All source.
- A passing autograder log on RISC-V.
- `WRITEUP.md` covering:
  - Why is `fence.i` required even though we set `PROT_EXEC` *after*
    writing?
  - What happens (in detail) if you forget the cache flush? Try it.
  - On a multi-core host, when would you need to broadcast an IPI to
    flush other CPUs' I-caches?

## Rubric

| Criterion | Points |
| --------- | ------ |
| Allocator achieves W^X discipline | 15 |
| Reloc application correct for required types | 25 |
| Programs run and return expected values | 25 |
| Cross-host refusal works on non-RISC-V | 10 |
| Benchmark numbers reported sensibly | 10 |
| Writeup quality and AI attribution | 15 |
| **Total** | **100** |

## Common pitfalls

- Forgetting that on RISC-V, *data* coherence is automatic but
  *instruction* coherence requires `fence.i`.
- Assuming the kernel's IPI shootdown covers user-space JITs. It
  doesn't; `__builtin___clear_cache` calls `__riscv_flush_icache(2)`
  which does the broadcast for you.
- Trying to debug with a debugger that sets breakpoints by writing
  `ebreak` — the cache flush you forgot will hide the problem in
  spectacular ways.

## What's next

Lab 07 reverses the polarity: you run MERLIN-V on a non-RISC-V host by
translating RISC-V to x86\_64 in a small, single-pass JIT.
