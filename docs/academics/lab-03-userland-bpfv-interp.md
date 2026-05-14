# Lab 03 — User-space BPF-V Interpreter

Module: 3
Effort tier: M
Prerequisites: Lab 02.
Design docs: [`02-isa-and-bytecode.md`](../design/02-isa-and-bytecode.md),
[`07-jit-and-offload.md`](../design/07-jit-and-offload.md).

## Learning objectives

- Decode RV32I instructions from a raw byte stream.
- Maintain an architectural register file and a small stack in
  user-space C.
- Execute a real (`-march=rv32i -mabi=ilp32`) program compiled by GCC.
- Implement the BPF-V helper ABI: `ecall` with `a7 = helper_id`.

## Background reading

- `docs/design/02-isa-and-bytecode.md` — §3, §5, §6.
- *RISC-V Instruction Set Manual*, Vol I, chapter 2 (RV32I).

## Specification

You will build `bpfvi`, a user-space interpreter.

Inputs:
- An ELF object compiled with `riscv32-unknown-elf-gcc -march=rv32i
  -mabi=ilp32 -nostdlib -ffreestanding -Wl,-Ttext=0x0` (the lab
  skeleton ships the linker script).
- The name of the entry symbol (default: `bpfv_entry`).
- A buffer to pass as the program's single `a0` argument.

Outputs:
- The 32-bit value the program returns in `a0`.
- An execution trace on stderr when run with `-t`.

### Required ISA support (subset of RV32I)

| Group | Instructions |
| ----- | ------------ |
| Reg-imm ALU | `addi`, `slti`, `sltiu`, `andi`, `ori`, `xori`, `slli`, `srli`, `srai`, `lui`, `auipc` |
| Reg-reg ALU | `add`, `sub`, `sll`, `slt`, `sltu`, `xor`, `srl`, `sra`, `or`, `and` |
| Branches | `beq`, `bne`, `blt`, `bge`, `bltu`, `bgeu` |
| Jumps | `jal`, `jalr` |
| Loads | `lb`, `lh`, `lw`, `lbu`, `lhu` |
| Stores | `sb`, `sh`, `sw` |
| Misc | `fence` (no-op for now), `ecall` (helper trap) |

You may **omit** `M` (mul/div), `A` (atomics), and `C` (compressed)
for this lab. Programs in the test suite are guaranteed not to use
them.

### Helper ABI

The interpreter treats `ecall` as follows:

1. Read `a7` (helper id).
2. Dispatch through a table:

   ```c
   typedef int32_t (*bpfvi_helper_fn)(uint32_t a0, uint32_t a1, uint32_t a2,
                                      uint32_t a3, uint32_t a4, uint32_t a5);

   static const struct bpfvi_helper {
       const char       *name;
       bpfvi_helper_fn   fn;
   } helpers[] = {
       [0] = { "trace_log",  helper_trace_log  },
       [1] = { "ctx_load_u32", helper_ctx_load_u32 },
       /* ... */
   };
   ```

3. Place the return value in `a0`.

The lab skeleton ships definitions for helpers 0 and 1; you must add
helper 2 (`mem_read_u8(ptr, off)`) and helper 3 (`mem_write_u8(ptr,
off, val)`), both bounds-checked against a user-supplied buffer.

### Trace format

On `-t`, every executed instruction prints:

```
PC=0x<pc>  INSN=0x<raw>  <mnemonic>  a0=<...> a1=<...> sp=<...>
```

The autograder pins this format, so do not deviate.

## Tasks

### Task 1 — Decoder

Implement `src/decode.c` exporting:

```c
struct bpfvi_insn {
    uint32_t pc;
    uint32_t raw;
    uint8_t  op;       // your internal enum
    uint8_t  rd, rs1, rs2;
    int32_t  imm;
};

int bpfvi_decode(const uint8_t *bytes, uint32_t pc, struct bpfvi_insn *out);
```

The function returns 0 on success and -1 on "not a supported insn."

### Task 2 — Interpreter core

Implement `src/interp.c`:

```c
struct bpfvi_state {
    uint32_t x[32];        // RV32 GPRs
    uint32_t pc;
    uint8_t  mem[64 * 1024]; // flat memory
    int      tracing;
};

int32_t bpfvi_run(struct bpfvi_state *st, uint32_t entry);
```

Make sure `x[0]` always reads as zero, regardless of writes.

### Task 3 — ELF loader

Implement `src/loader.c`:

```c
int bpfvi_load_elf(const char *path, struct bpfvi_state *st,
                   uint32_t *entry_out, const char *entry_sym);
```

The loader places `PT_LOAD` segments into `st->mem` at their virtual
addresses and resolves `entry_sym` against the ELF's symbol table.

### Task 4 — Helper dispatch

Implement `src/helpers.c` and the dispatch path in `interp.c` so that
`ecall` traps, reads `a7`, calls the right helper, and stuffs the
return into `a0`.

### Task 5 — End-to-end test

The skeleton ships `tests/progs/`:

- `tests/progs/sum.S` — sums `a0..a4`, returns in `a0`.
- `tests/progs/loop.S` — counts to 10 in a loop, returns 10.
- `tests/progs/strlen.S` — Lab 02's `strlen` adapted to call helper 2
  for each byte.
- `tests/progs/trace.S` — calls helper 0 with the literal "BPFV03".

`tools/grade.sh` builds each `.S`, runs your interpreter, and compares
output to a golden trace.

## Deliverables

- `src/decode.c`, `src/interp.c`, `src/loader.c`, `src/helpers.c`,
  `include/bpfvi.h`.
- A passing autograder log.
- `WRITEUP.md` answering:
  - How is `x[0]` enforced as zero? At decode time or at write time?
    Both? Why does it matter for the verifier (Lab 04)?
  - What invariant guarantees `pc` always points to a 4-byte aligned
    address?
  - Where in your code would you add support for the `C` (compressed)
    extension, and what would change about the decoder?

## Rubric

| Criterion | Points |
| --------- | ------ |
| Decoder covers required subset | 20 |
| Interpreter correctly executes RV32I subset | 25 |
| ELF loader handles `PT_LOAD` and symbol lookup | 15 |
| Helper dispatch (incl. helpers 2 and 3) correct | 15 |
| All five test programs pass | 15 |
| Writeup quality and AI attribution | 10 |
| **Total** | **100** |

## Common pitfalls

- Sign-extension: many RV32I instructions sign-extend an immediate
  *before* zero-extending it to 32 bits. Get this wrong and `addi t0,
  zero, -1` ends up as 0x0000_0FFF instead of 0xFFFF_FFFF.
- `jalr` clears bit 0 of the computed target. Easy to miss.
- Branches use offsets *from the current PC*, not the next PC.
- `lui` writes a 32-bit immediate with the low 12 bits zero. `auipc`
  adds that to the *current* PC.

## What's next

Lab 04 wraps a verifier around this interpreter: register-state
abstract interpretation that proves the program is safe *before* you
let it run.
