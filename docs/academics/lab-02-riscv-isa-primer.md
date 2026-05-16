# Lab 02 — RISC-V ISA Primer

Module: 2
Effort tier: M
Prerequisites: Lab 00.
Design doc: [`docs/design/02-isa-and-bytecode.md`](../design/02-isa-and-bytecode.md).

## Learning objectives

After this lab you will be able to:

- Identify every RV32IM and RV64IM instruction in compiler output by
  opcode field, without consulting a chart.
- Explain the RISC-V calling convention (lp64 / ilp32) at the level
  needed to reason about helper invocation.
- Read the differences between `-march=rv64imac` and `-march=rv64gc`
  output.
- Hand-write small RISC-V assembly that compiles, links, and runs
  under QEMU user mode.

## Background reading

- *The RISC-V Reader* (Patterson & Waterman), short read, do this
  first.
- *RISC-V Instruction Set Manual*, Vol I (User-Level ISA), chapters
  2–4 (RV32I, RV64I, M, A, C extensions).
- *RISC-V psABI*: <https://github.com/riscv-non-isa/riscv-elf-psabi-doc>

## Tasks

### Task 1 — Anatomy of `add`

For each of the following snippets, predict the emitted instruction
sequence, *then* compile and check:

```c
int add_si(int a, int b)         { return a + b; }
long add_di(long a, long b)      { return a + b; }
unsigned add_ui(unsigned a, unsigned b) { return a + b; }
```

Use:

```sh
riscv64-linux-gnu-gcc -O2 -S -mabi=lp64 -march=rv64imac \
    -fno-stack-protector add.c -o add.s
```

Annotate `add.s` with the binary encoding (use `riscv64-linux-gnu-objdump
-d add.o` to confirm).

### Task 2 — Calling convention

Write a function that takes 9 arguments and returns the sum. Compile
and look at the assembly. Answer in `WRITEUP.md`:

- Which arguments are in `a0`–`a7`?
- Where does the 9th argument live?
- Where does the return value live?
- Who is responsible for stack alignment, and to what boundary?

### Task 3 — Loads, stores, and addressing modes

Implement, in C, a function that:

1. Loads a byte from `*p`,
2. Sign-extends it to 32 bits,
3. Stores it back at `p[4]`,
4. Returns the sign-extended value.

Compile with both `-O0` and `-O2`. For each, identify:

- The base register and the immediate offset.
- The exact RISC-V opcodes used (`lb`, `lbu`, `sb`, `lw`, `sw`, …).

In `WRITEUP.md`, list every load/store instruction the resulting
binary uses and the addressing form.

### Task 4 — Compressed instructions (`C`)

Recompile Task 1 with and without `-march=rv64imac` (which enables
`C`). Diff the assembly. In `WRITEUP.md`:

- Which instructions were 16-bit (`c.*`) instead of 32-bit?
- Why does the verifier in [`docs/design/06-verifier.md`](../design/06-verifier.md)
  care?

### Task 5 — Hand-written assembly

Write `src/strlen.S`:

```asm
# SPDX-License-Identifier: GPL-2.0
        .text
        .globl  my_strlen
my_strlen:
        mv      a1, a0          # a1 = original pointer
1:      lbu     a2, 0(a0)
        beqz    a2, 2f
        addi    a0, a0, 1
        j       1b
2:      sub     a0, a0, a1
        ret
```

Build:

```sh
riscv64-linux-gnu-gcc -nostdlib -static -O2 \
    src/strlen.S src/main.c -o build/strlen
qemu-riscv64-static build/strlen
```

Where `main.c` calls `my_strlen("hello, MERLIN-V")` and exits with the
length.

### Task 6 — Disassemble eBPF's RISC-V JIT output

Reuse Lab 01's `xdp_drop.bpf.o`. Run it on `qemu-system-riscv64` Linux
from Lab 00 and dump the JIT (Lab 01 Task 3). Match each emitted
RISC-V instruction to a part of the eBPF instruction stream. Save the
annotated result to `build/rv64_jit_annotated.txt`.

### Task 7 — Forbidden instructions

For each instruction below, locate (by opcode bits) which extension
it belongs to and whether it's allowed in MERLIN-V's default in-kernel
profile (per `docs/design/02-isa-and-bytecode.md` §3):

1. `csrr t0, mcycle`
2. `ecall`
3. `mret`
4. `fence.i`
5. `amoadd.w t0, t1, 0(t2)`
6. `fadd.s f0, f1, f2`
7. `c.addi sp, -16`
8. `vsetvli t0, t1, e8, m1`

## Deliverables

- Annotated `add.s`, `loadstore.s`, hand-written `strlen.S`.
- `build/rv64_jit_annotated.txt`.
- `WRITEUP.md` answering Tasks 2, 4, and 7 in detail.

## Rubric

| Criterion | Points |
| --------- | ------ |
| Task 1: opcodes and encodings identified | 15 |
| Task 2: calling convention answered correctly | 15 |
| Task 3: load/store table complete | 15 |
| Task 4: compressed-vs-uncompressed diff explained | 10 |
| Task 5: hand-written `strlen` runs and returns 12 | 15 |
| Task 6: eBPF JIT annotated | 20 |
| Task 7: profile categorisation correct | 10 |
| **Total** | **100** |

## Common pitfalls

- Reading `objdump` output without `-Mnumeric` and getting confused
  by ABI names vs register numbers.
- Forgetting `-fno-asynchronous-unwind-tables` and being surprised by
  CFI directives in the `.s` file.
- Mixing compressed-instruction-enabled and -disabled builds in the
  same `build/` and not noticing.

## What's next

Lab 03 starts the MERLIN-V implementation: a small user-space interpreter
for an RV32I subset, executing real ELF-packaged programs.
