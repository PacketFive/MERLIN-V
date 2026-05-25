# 15 — Verifier Phase-2 architecture (landed)

Status: implemented (kernel + userland; 14/14 fixtures pass)
Owner: PacketFive
Cross-refs: [06-verifier.md](06-verifier.md), [02-isa-and-bytecode.md](02-isa-and-bytecode.md)

This document records the Phase-2 verifier as actually landed. It
supersedes the "Phase 2 work items" column of
[06-verifier.md](06-verifier.md) §9.

## 1. What Phase-2 closes

Phase-1 (the original linear-pass prototype) had four well-known
gaps that any LKML reviewer would notice in the first pass:

1. No CFG. Every program was walked top-to-bottom; branches were
   not followed; merges were not modeled. A program with two paths
   that wrote different values to the same register before merging
   would be silently accepted with whichever value the linear walk
   happened to compute last.
2. No state joins. Even if the CFG existed, there was nowhere to
   merge abstract states from multiple incoming paths.
3. No range tracking. `Const(K)` and `Unknown` were the only scalar
   shapes. Two paths producing `Const(3)` and `Const(5)` joined to
   `Unknown` rather than the precise `Scalar{[3..5]}`.
4. No loop story at all. Back edges were a hard reject. The eBPF
   verifier eventually grew a `bpf_loop()` helper; we owed a
   counterpart.

Phase-2 closes all four.

## 2. Files

| File | Role |
| ---- | ---- |
| `kernel/merlin/include/merlin_tnum.h`        | tnum + scalar range domain (kernel) |
| `tools/merlin-verifier/tnum.h`               | same (userland) |
| `kernel/merlin/cfg.c`                        | CFG construction + dominators + back-edge identification (kernel) |
| `tools/merlin-verifier/cfg.{h,c}`            | same (userland) |
| `kernel/merlin/verifier.c`                   | worklist abstract interpreter (kernel) |
| `tools/merlin-verifier/verify.{h,c}`         | same (userland) |
| `tools/merlin-verifier/bad-progs/run.sh`     | executable spec (14 cases) |

The kernel and userland share an abstract domain at the source
level: `merlin_tnum.h` is intentionally header-only and dependency-
free; the userland version is the same algorithm with C99
`uint64_t/int64_t` instead of kernel `u64/s64`. This keeps the
eventual factoring-into-shared-module path (Option B → Option A in
[06-verifier.md §4](06-verifier.md)) mechanical.

## 3. The abstract domain

Per-register abstract value (see `enum merlin_rval_kind`):

```
RVAL_UNINIT             register has not been written on any path that
                        reaches this point (bottom)
RVAL_SCALAR             integer scalar with:
                          - signed range [smin, smax]
                          - unsigned range [umin, umax]
                          - tnum (mask of unknown bits + value of
                            known bits) per Vishwanathan 2022
RVAL_PTR_CTX            pointer to the ctx, with [off_min, off_max]
RVAL_PTR_STACK          pointer to the current frame, with
                        [off_min, off_max]
RVAL_PTR_HELPER_RET     pointer just returned from helper `helper_id`
```

The join `⊔` on this lattice:

- `Uninit ⊔ x = x ⊔ Uninit = x`
- `Scalar(a) ⊔ Scalar(b) = Scalar(scalar_join(a,b))` (pointwise
  widen-range, pointwise tnum-join).
- `Ptr(k, lo₁..hi₁) ⊔ Ptr(k, lo₂..hi₂) = Ptr(k, min(lo)..max(hi))`
- Mixed kinds (e.g. `Scalar ⊔ PtrCtx`) → `Scalar top` (sound
  but imprecise; programs that hit this typically have a bug).

Equality is bit-for-bit on all fields; the worklist iterates until
no entry-state changes.

## 4. CFG construction

`merlin_cfg_build()` (cfg.c) runs in three passes:

1. **Leader pass.** Scan all instructions; mark block leaders.
   Leaders are: byte 0; every instruction immediately following a
   branch / JAL / JALR (return); every direct branch / JAL target.
   JALR targets are not leaders because the verifier rejects
   non-return indirect jumps.
2. **Block pass.** Walk the instruction stream; whenever a leader
   bit is set, finalise the previous block and open a new one.
   Each block records its terminator instruction; successor PCs
   are extracted from the terminator's `imm`.
3. **Edge resolution.** For every block, look up its successor PCs
   in the (block-leader, block-index) table and store the indices.

After this, `merlin_cfg_compute_rpo()` runs an iterative DFS to
produce a reverse-postorder, and `merlin_cfg_compute_dominators()`
uses Cooper-Harvey-Kennedy iterative on the RPO to compute
immediate dominators. A back edge is any edge `(b → s)` where `s`
dominates `b`. Programs are accepted only if every back edge is
gated (see §6).

## 5. The worklist

```c
ws.push(block 0)
entry_state[0] = vstate_init_entry()
while ws is non-empty:
    b = ws.pop()
    state = entry_state[b]              // start from joined state
    for each instruction in b:
        step_insn(insn, &state, ...)    // transfer function
    for each successor s of b:
        if vstate_join(entry_state[s], state) changed:
            ws.push(s)
```

A global `steps` counter guards against irreducible CFGs slipping
past the back-edge check; if `steps > step_cap` the program is
rejected with an explicit diagnostic. The default `step_cap` is
2²⁰ (one million transfer invocations); per-profile overrides land
in `merlin_verifier_cfg_for_prog()`.

The state is *not* deep-copied on the kernel stack: the worklist
keeps an off-stack scratch `merlin_vstate` and `memcpy`s the entry
state into it at the start of each block iteration. Without this,
`-Wframe-larger-than=1024` fires on the 32-rval × ~76-byte struct.

## 6. Bounded loops: `merlin_loop_bound()`

`merlin_loop_bound(N)` is a runtime no-op helper at id `0x0142`
that the verifier recognises as a loop-bound annotation. The
canonical emitted sequence is:

```
li   a7, 0x0142          ; helper id = LOOP_BOUND
li   a0, <const N>       ; trip-count upper bound
ecall                    ; verifier marks the next block as a
                         ;   permitted loop header
<.L>:                    ; loop body
  ...
  addi a0, a0, -1
  bne  a0, x0, <.L>      ; back edge accepted iff <.L> was the
                         ;   block immediately following the ecall
```

The verifier maintains a `loop_header_ok[]` bitmap by block index;
after walking a block, if a `merlin_loop_bound()` ecall was seen,
the fall-through successor block is marked. At the end of the
worklist pass, every back-edge destination must be in
`loop_header_ok[]` or the program is rejected.

This is intentionally a *syntactic* check (the verifier does not
attempt to prove that the counter is actually decremented inside
the loop body — the runtime separately enforces a hard dynamic
trip cap on every program). The combination of `loop_bound`
annotation + runtime cap is sound: the program cannot run more
iterations than declared regardless of what the body does.

A future Phase-3 iteration may extend this to the full eBPF-style
`merlin_loop(N, cb, ctx)` callback form, with a separate
verification context for the callback subgraph.

## 7. Test battery

`tools/merlin-verifier/bad-progs/run.sh` is the executable spec.
Phase-2 additions:

| Case             | Expected | What it tests |
| ---------------- | -------- | ------------- |
| `loop_bounded`   | ACCEPT   | Back edge after `merlin_loop_bound(10)` |
| `loop_unbounded` | REJECT   | Back edge without the annotation |
| `join_scalars`   | ACCEPT   | Diamond CFG: two paths set a0 = 1 vs a0 = 5, then return |
| `alu_two_ptrs`   | REJECT   | `add s0, sp, a0` where both operands are pointers |

All 14 cases (10 Phase-1 + 4 Phase-2) pass.

## 8. What Phase-3 still owes

Phase-2 closes the LKML "show me your safety story" gate but does
not close everything in `06-verifier.md §2`. The remaining items
become Phase-3:

- **Stack discipline.** ✅ Landed (Phase-3.A1). Track `sp` deltas
  from entry; reject any write to `sp` other than `addi sp, sp, K`;
  enforce `[-cfg.max_stack_bytes, 0)` as the valid frame range;
  reject stores/loads whose [abs_off, abs_off+width) escapes the
  frame.  4 new fixtures: `stack_legal_frame` (ACCEPT),
  `stack_store_above_sp` / `stack_budget_overflow` /
  `stack_sp_clobber` (all REJECT).  18/18 fixtures pass.
- **kfunc resolution.** ✅ Landed (Phase-3.A2). Adds
  `RVAL_PTR_KFUNC_SLOT` rval kind and `cfg.kfunc_allow[]` bitset.
  Programs invoke a registered kfunc via the canonical sequence:
  ```
  addi a0, x0, <kfunc_id>          ; constant kfunc id
  addi a7, x0, 0x143               ; MERLIN_HELPER_KFUNC_RESOLVE
  ecall                            ; a0 -> PTR_KFUNC_SLOT(id)
  jalr x0/ra, a0, 0                ; verified indirect call
  ```
  The verifier accepts only two JALR patterns: return (`rs1 == ra`)
  and kfunc tail-call (`rs1` holds a `PTR_KFUNC_SLOT` whose id is
  in the per-program kfunc allowlist).  Every other indirect JALR
  is rejected with a precise diagnostic. 4 new fixtures:
  `kfunc_call_legal` (ACCEPT), `kfunc_unallowed` /
  `jalr_arbitrary_reg` / `jalr_unresolved_ptr` (all REJECT).
  22/22 fixtures pass.
- **`merlin_loop()` (callback form).** Verify the body as a
  separate verification unit with the loop-counter scalar in `a0`.
- **CO-RE-V effect modeling.** Substitute relocation effects before
  verification.
- **Atomic / fence semantics.** Profile-gated.

Each is a self-contained patch on top of Phase-2.
