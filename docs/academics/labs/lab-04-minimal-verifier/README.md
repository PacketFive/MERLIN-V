# Lab 04 — Minimal Verifier

The Lab 04 statement is in `docs/academics/lab-04-minimal-verifier.md`.

## Files

| File | Status | What you do |
|------|--------|-------------|
| `Makefile` | PROVIDED | `make`, `make test`, `make grade` |
| `src/verify.h` | PROVIDED | Abstract domain + interface |
| `src/verify.c` | SKELETON | Implement the verifier |
| `src/main.c` | PROVIDED | CLI driver |
| `tests/accept/` | PROVIDED | Programs the verifier MUST accept |
| `tests/reject/` | PROVIDED | Programs the verifier MUST reject |

## Build / run

```bash
make                         # builds merlin-verify
./merlin-verify tests/accept/01-trivial.bin     # exit 0
./merlin-verify tests/reject/01-bad-load.bin    # exit non-zero

make test                    # runs all accept + reject cases
make grade                   # autograder
```

## What you implement

In `src/verify.c`, the function `merlin_verify_text()`. The skeleton
provides the linear-pass loop and decoder driver; you supply the
abstract-interpretation logic.

The abstract domain (declared in `verify.h`):

```
RVAL_UNKNOWN              top
RVAL_CONST(value)         fully known
RVAL_PTR_CTX(off)         pointer to ctx + offset
RVAL_PTR_STACK(off)       pointer to current stack frame + offset
RVAL_PTR_HELPER_RET(id)   pointer returned from helper `id`
```

Rejection criteria (a program is rejected if **any** of these fire):

1. Instruction outside the lab subset (decoder sets `INSN_INVALID` or
   `INSN_FORBIDDEN`).
2. Back-edge: JAL or BRANCH with `imm < 0`.
3. `ecall` with `a7` not constant, or constant not in the helper
   allowlist.
4. LOAD/STORE through a non-pointer register
   (`UNKNOWN` or `CONST` source).

ALU effects you must model:

- `addi rd, rs1, imm`: if `rs1 == x0`, `rd = CONST(imm)`. If `rs1` is
  a pointer, `rd` keeps the same pointer-root with `offset + imm`.
- `lui rd, imm`: `rd = CONST(imm << 12)` (handled by the U-type
  immediate).
- Any other instruction: clobber `rd` to `UNKNOWN`.

You can copy the structure of the userland prototype at
`tools/merlin-verifier/` (especially `verify.c`) — it implements
exactly this algorithm at a slightly larger scale.

## Hint

Write the rejection cases first. It's much easier to keep an honest
verifier on a hostile test set than to add hostility checks to a
permissive verifier later.

## What's graded

- 100% of `tests/accept/*.bin` accepted.
- 100% of `tests/reject/*.bin` rejected.
- Rejection reason printed to stderr is reasonable (autograder
  greps for keywords like "back-edge", "non-pointer", "helper").
- No use of `goto` to exit the verification loop except for a
  well-named `reject:` label.
- Clean `-Wall -Wextra` at `-O2`.
