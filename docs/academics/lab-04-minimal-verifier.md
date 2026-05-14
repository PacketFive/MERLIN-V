# Lab 04 — Minimal Verifier

Module: 3
Effort tier: L
Prerequisites: Lab 03.
Design doc: [`docs/design/06-verifier.md`](../design/06-verifier.md).

## Learning objectives

- Build an abstract interpreter over an ISA you already wrote a
  concrete interpreter for.
- Track register types (scalar, pointer-to-ctx, pointer-to-stack),
  value ranges, and pointer offsets.
- Reject programs that violate the BPF-V safety properties listed in
  `docs/design/06-verifier.md` §2.
- Explain why the soundness of the verifier matters more than its
  precision.

## Background reading

- `docs/design/06-verifier.md` (whole document).
- `bpf-next/kernel/bpf/verifier.c` — read the file from the top down
  to about `check_reg_arg`. Don't try to read the whole thing; sample
  the structure.
- Gershuni et al., PLDI '19 paper on the eBPF verifier (linked from
  `docs/academics/README.md` §5.2).

## Specification

You will build `bpfvi-verify`. Inputs:

- The same ELF format Lab 03 consumes.
- A program-type record describing the context type (e.g. "ctx is a
  pointer to 32 readable bytes").

Outputs:
- Exit 0 if the program is accepted.
- Exit non-zero with a structured log explaining the first rejection.

### Abstract domain

For each register `x[i]`, track:

```c
enum bpfvi_type {
    BPFVI_T_INVALID,
    BPFVI_T_UNKNOWN,         // scalar, no info
    BPFVI_T_CONST,           // scalar, known value
    BPFVI_T_RANGE,           // scalar with [umin, umax]
    BPFVI_T_PTR_CTX,         // pointer to ctx, with offset range
    BPFVI_T_PTR_STACK,       // pointer to program stack, with offset
    BPFVI_T_PTR_HELPER_RET,  // typed return of a helper
};

struct bpfvi_reg {
    enum bpfvi_type type;
    int64_t  val;        // for CONST
    uint64_t umin, umax; // for RANGE / pointer offsets
};
```

`x[0]` is permanently `T_CONST`, value 0.

The stack is tracked at byte granularity for the first `BPFVI_STACK`
bytes (e.g., 256), with a per-byte type tag (unknown / scalar /
pointer).

### Required checks

The verifier must reject a program if any of the following holds at
any reachable program point:

1. An instruction outside the allowed RV32I subset (Lab 03's subset).
2. A load/store address whose `rs1` is not a pointer type *or* whose
   computed effective address falls outside the allowed bounds for
   that pointer's base.
3. A `jalr` whose target is not a verifier-known function entry.
4. A read of an uninitialised register or stack slot.
5. A loop without a provable iteration bound (use a simple
   conservative analysis: dominator + back-edge detection plus a
   per-back-edge counter capped at `BPFVI_MAX_LOOP_ITERS`).
6. An `ecall` whose `a7` is not a `T_CONST` in the helper allow list.
7. A division-by-zero (only relevant if you optionally support `M`).

### Required passes

- **Pass 0**: build the CFG. Reject if the CFG is irreducible.
- **Pass 1**: dataflow / abstract interpretation to fixpoint with
  widening at loop headers.
- **Pass 2**: helper-call ABI checks at every `ecall` site.

## Tasks

### Task 1 — CFG builder

Implement `src/cfg.c`. Produce basic blocks and successor sets.
Reject irreducible CFGs (no entry-dominated header) with a clear
message.

### Task 2 — Abstract state

Implement `src/abs_state.c`. Provide `meet`, `widen`, and `assert_*`
operations. Get this right; the rest of the verifier rides on it.

### Task 3 — Transfer functions

Implement per-instruction transfer functions in `src/transfer.c`. For
example:

- `addi rd, rs1, imm`:
  - If `rs1` is `T_CONST(v)` then `rd ← T_CONST(v + imm)`.
  - If `rs1` is `T_RANGE(umin, umax)` then `rd ← T_RANGE(umin + imm,
    umax + imm)` with saturation.
  - If `rs1` is a pointer with offset range `[lo, hi]` then `rd ←`
    same pointer with `[lo + imm, hi + imm]`.

Document every choice in `WRITEUP.md`.

### Task 4 — Verifier driver

Implement `src/verify.c` that:

1. Parses the ELF using your loader from Lab 03.
2. Builds the CFG.
3. Initialises the entry state: `a0` is `T_PTR_CTX` with offset 0 and
   ctx size from the program-type record.
4. Runs to fixpoint.
5. Emits a verifier log to stderr in a structured format the lab's
   golden tests compare against.

### Task 5 — Test corpus

The skeleton ships `tests/good/` and `tests/bad/`:

- `tests/good/sum.S`, `tests/good/loop.S` — must be accepted.
- `tests/bad/oob_read.S` — reads past `ctx`. Must be rejected with a
  pointer-bounds error.
- `tests/bad/wild_jalr.S` — `jalr` through a computed value. Must be
  rejected.
- `tests/bad/unbounded_loop.S` — loop with no provable bound. Must
  be rejected.
- `tests/bad/uninit_reg.S` — reads `t3` without writing it. Must be
  rejected.
- `tests/bad/bad_ecall.S` — `ecall` with `a7` unknown. Must be
  rejected.

For each bad test, the rejection message must:
- Cite the PC of the offending instruction.
- Name the property violated (matching §"Required checks" above).

### Task 6 — Run the accepted programs

Wire the verifier into the interpreter from Lab 03: if `bpfvi-verify`
accepts, `bpfvi` runs the program; otherwise it refuses to. Add a
`--unsafe` flag to skip the verifier for debugging only.

## Deliverables

- All source under `src/` and `include/`.
- A passing autograder log.
- `WRITEUP.md` covering:
  - Your widening strategy at loop headers.
  - Where your verifier is *sound but imprecise* (i.e., would reject
    a safe program that a smarter verifier would accept). Give an
    example.
  - One bug you fixed during development that, if shipped, would have
    let an unsafe program through. Describe how you found it.

## Rubric

| Criterion | Points |
| --------- | ------ |
| CFG builder rejects irreducible CFGs correctly | 10 |
| Abstract domain `meet`/`widen` correct on supplied unit tests | 20 |
| Transfer functions complete for the RV32I subset | 25 |
| All `tests/good/` accepted | 10 |
| All `tests/bad/` rejected with the right property name | 20 |
| Integration with Lab 03 interpreter works | 5 |
| Writeup quality and AI attribution | 10 |
| **Total** | **100** |

## Common pitfalls

- Forgetting to widen at a loop header → infinite analysis.
- Widening too aggressively → losing precision on values the verifier
  needs to reason about. Pick a small `n` (e.g. unroll 4× then widen).
- Confusing *concrete* PC arithmetic with *abstract* PC values.
  Branch targets are always concrete in this lab; abstract values
  flow only into general registers.
- Treating `lw rd, 0(rs1)` as safe just because `rs1` is "some
  pointer." It is only safe if the *offset range* fits in the base
  pointer's region.

## What's next

Lab 05 turns these scattered files into the `bpfv-objtool` user-space
tool, the BPF-V counterpart of `bpftool` for the object format itself.
