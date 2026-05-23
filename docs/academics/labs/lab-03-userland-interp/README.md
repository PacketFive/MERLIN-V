# Lab 03 — User-space MERLIN-V Interpreter

The Lab 03 statement is in
`docs/academics/lab-03-userland-merlin-interp.md`.

This directory contains the starter code.

## Files

| File | Status | What you do |
|------|--------|-------------|
| `Makefile` | PROVIDED | `make`, `make test`, `make grade` |
| `src/interp.h` | PROVIDED | Public interface |
| `src/interp.c` | PARTIALLY PROVIDED | Fill in TODO blocks: `execute_alu_imm`, `execute_load`, `execute_store`, `execute_branch`, `execute_ecall` |
| `src/decode.c` | PROVIDED | Reference RV32I decoder |
| `src/main.c` | PROVIDED | CLI driver: parses args, loads ELF, calls `interp_run` |
| `tests/` | PROVIDED | Hand-rolled ELF blobs and expected outputs |

## Build / run

```bash
make
./merlin tests/01-addi.bin              # should print 42
./merlin -t tests/02-loop-bounded.bin   # trace mode

make test     # runs all tests/*.bin and compares to expected.txt
make grade    # autograder
```

## Scope

The interpreter supports a strict subset of RV32I (the labs document
the exact list). You do **not** need to implement floating point,
the C extension, AMO, CSR, mret/sret/wfi, or anything else outside
the subset.

## Helper ABI

The interpreter implements three helpers (see `common/rv32.h`):

| ID | Name | Semantics |
|----|------|-----------|
| 1 | `trace_print(ptr)` | Print null-terminated string at `a0`; return 0 |
| 2 | `get_time_ns()` | Return monotonic time in ns |
| 3 | `rand_u32()` | Return a `rand()` value |

`ecall` semantics: `a7` holds the helper id, `a0..a5` are args,
return goes into `a0`.

## Suggested approach

1. Read `src/interp.h` and `src/decode.c` end-to-end before writing
   any code. The decoder gives you a `struct rv_insn` per
   instruction — you decide what to do with it.
2. The TODO blocks in `src/interp.c` are independent; start with
   `execute_alu_imm` (the simplest), get tests `01-addi.bin` and
   `02-loop-bounded.bin` passing, then proceed.
3. Run `make test` after each TODO.

## What's graded

- All `tests/*.bin` produce the expected stdout and exit code.
- Trace mode (`-t`) produces a well-formed per-instruction trace
  (autograder checks the structure, not the exact format).
- No use of `goto` to skip the execution loop. (Style.)
- No undefined behaviour at `-Wall -Wextra` (autograder rebuilds
  with `-Werror`).
