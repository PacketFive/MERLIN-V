# 06 — Verifier Strategy

Status: sharing strategy pinned (Option B for v1); profile catalogue aligned
Owner: PacketFive
Last reviewed: post profile + ABI decisions

The verifier is *the* load-bearing safety component. This document
records the strategy for verifying programs whose ISA is a general
RISC-V profile rather than a custom restricted ISA.

## 1. The hard part

eBPF's verifier is tractable in large part because the eBPF ISA is
*designed for verifiability*: no general indirect jumps, no raw
pointer arithmetic outside typed roots, a small opcode set, no
self-modifying code, no FP. RISC-V was designed for *execution*, not
verification, so we cannot simply lift the eBPF verifier onto a stock
RISC-V profile.

The MERLIN-V verifier therefore combines two ideas:

1. **ISA-profile restrictions** that bring RISC-V closer to a
   verifiable subset (§2 of [02-isa-and-bytecode.md](02-isa-and-bytecode.md)).
2. **Abstract interpretation** over the resulting profile, tracking
   the same things eBPF's verifier tracks (register types, ranges,
   alignment, pointer provenance, liveness), driven by MERLIN BTF (§3 of
   [04-toolchain.md](04-toolchain.md)).

## 2. Properties verified

For a program to be accepted:

1. **Profile compliance.** Every instruction belongs to the declared
   profile. No privileged instructions, CSRs, `fence.i`, or vector
   instructions (in the default profile).
2. **Control flow.**
   - The CFG is reducible.
   - Every indirect branch (`jalr`) targets either (a) a return to an
     established frame, or (b) a kfunc table entry whose value the
     verifier knows to be a registered kfunc pointer.
   - All loops are bounded (compile-time-known iteration count) or
     use the project-supplied `merlin_loop()` helper.
3. **Stack discipline.**
   - `sp` is decremented at function entry by a compile-time constant
     ≤ profile-max stack size.
   - No stores below `sp` past the declared frame.
   - The frame pointer (`x8`/`s0`) is established and not stomped.
4. **Pointer provenance.** Every load/store address resolves to a
   typed root pointer (ctx, map value, packet bounds, stack, helper
   return). Arbitrary `add reg, reg` of two unconstrained values is
   not a valid address.
5. **Helper / kfunc ABI compliance.** At every `ecall`, `a7` is a
   compile-time constant in the program-type's helper set and the
   types in `a0`–`a5` match. At every kfunc `jalr`, the resolved
   target is in the per-program-type allow list.
6. **Termination / runtime bound.** Either a static iteration bound is
   provable, or the program opts into `merlin_loop()` with an explicit
   runtime-checked cap.
7. **No FP** in the default profile.
8. **Memory model use.** Atomic instructions act on properly typed
   shared memory; explicit `fence`s have type-system-consistent
   effects.

## 3. Algorithmic core

The verifier is a textbook abstract interpreter over the CFG with the
following domains:

- **Register state:** for each architectural register, a structured
  abstract value:
  - `Unknown` (top)
  - `Scalar(value range, alignment)` — like eBPF's `tnum` plus min/max.
  - `Ptr(root, off-range)` where `root ∈ {ctx, map_value(M),
    pkt(begin..end), stack, kfunc(name), helper_ret(H)}`.
- **Memory state:** per (root, offset) typed slot tracking.
- **Path conditions:** branch predicates accumulated through the CFG.

State merge is by widening at loop headers, then re-walking until
fixpoint or step-cap exhaustion. The step cap is configurable per
profile; on `merlin-rtos-rv32` it is sharply tighter to keep
verification feasible on the loader CPU.

## 4. Sharing with the eBPF verifier (decided)

**Decision: Option B for RFC v1; Option A documented as a
follow-up.**

The two verifiers want the same *abstract domain*
(tnum/range/pointer provenance machinery) over different
*concrete ISAs*. There are two plausible factorings:

- **Option A — extract a shared `lib/bpf_absint/` module** from
  `kernel/bpf/verifier.c`, parameterise the per-insn transfer
  functions. eBPF and MERLIN-V each plug in their decoder + transfer
  function table.
- **Option B — independent verifier.** Standalone
  `kernel/merlin/verifier.c` that duplicates the abstract-domain
  machinery.

The MERLIN-V RFC v1 patchset lands as **Option B**. The patchset is
*self-contained*: it adds `kernel/merlin/` and does not modify
`kernel/bpf/`. Rationale:

- Lowest possible review surface. eBPF maintainers can review the
  MERLIN-V verifier as new code without auditing changes to an
  existing, security-critical file they already maintain.
- Fastest time-to-merge. Option A requires refactoring
  `kernel/bpf/verifier.c` *before* MERLIN-V can land; that refactor
  is its own multi-cycle effort.
- Lowest risk. A refactor of the eBPF verifier could regress eBPF
  behaviour; keeping MERLIN-V parallel decouples the two risk
  surfaces.

The duplication cost is real but bounded — abstract-domain code
is hundreds of lines, not thousands.

**Commitment for v1.** The MERLIN-V verifier's data structures are
designed *as if* they will be factored later: same abstract
domain (tnum + range + pointer provenance + liveness), same
widening discipline, same naming where possible. The eventual
refactor to Option A becomes a syntactic change, not a semantic
one. The RFC cover letter says this explicitly.

**Option A as a follow-up.** Once both verifiers are in the tree
and have shipped, a separate, opt-in patchset can extract the
shared abstract-domain module. That patchset is reviewed by both
BPF and MERLIN-V maintainers jointly. It is *not* a precondition for
either verifier landing.

## 5. Decoder

The verifier decodes RISC-V instructions itself rather than calling
out to a disassembler. The decoder lives in `kernel/merlin/decode.c`
and is generated from a machine-readable opcode table (likely
`riscv-opcodes` upstream, post-processed). The decoder produces a
small structured representation:

```c
struct merlin_insn {
    enum merlin_op   op;       // ADD, ADDI, LD, JAL, JALR, ECALL, ...
    u8             rd, rs1, rs2;
    s32            imm;
    u32            raw;      // original bytes (for cache/dump)
    u8             size;     // 2 (compressed) or 4
};
```

Compressed (`C`) instructions are expanded during decode so the
abstract interpreter sees a uniform 32-bit-ish stream.

## 6. Performance considerations

- Verification cost must scale acceptably to programs in the tens of
  thousands of instructions (XDP-V programs in particular can be
  large after CO-RE-V expansion).
- The step cap and the widening discipline are the two main knobs.
- Verifier benchmarks are part of the evaluation plan (see
  [01-ebpf-comparative-analysis.md](01-ebpf-comparative-analysis.md) §5).

## 7. Execution-context profiles ("can we run real apps?")

A repeated design-review question is whether, given an unrestricted C
dialect, a MERLIN-V program could be an arbitrary application running
inside the kernel JIT VM. The architectural answer is recorded in
[`00-overview.md`](00-overview.md) §7; this section is the
*verifier-engineering* counterpart and defines how the verifier
adapts to multiple execution contexts.

### 7.1 The verifier is parameterised by profile

The verifier is not one fixed checker. It is parameterised by an
**execution-context profile** that supplies:

- The permitted RISC-V instruction subset (see
  [02-isa-and-bytecode.md](02-isa-and-bytecode.md) §3).
- The verifier step cap.
- The maximum program text size and program stack size.
- Whether sleeping / page faults / blocking helpers are permitted.
- The helper set and kfunc allow list.
- Whether recursion is permitted, and to what depth.
- Whether the `A` (atomics), `V` (vector), or `F`/`D` (FP)
  extensions are admissible.
- The widening discipline at loop headers and the maximum loop
  unroll factor.

A program declares its profile in `.merlin.meta` (see
[02-isa-and-bytecode.md](02-isa-and-bytecode.md) §8). The loader
selects the verifier profile to run accordingly, capability-gated by
profile (different profiles may require different capabilities, e.g.
`CAP_MERLIN` for default in-kernel; `CAP_MERLIN_OFFLOAD` for offload to a
trusted device; `CAP_MERLIN_RTOS` for embedded; etc.). Default-deny
applies: an unknown profile is rejected.

### 7.2 Initial profile catalogue

The catalogue below ties verifier-side knobs to the two pinned
**bytecode** profiles in
[02-isa-and-bytecode.md](02-isa-and-bytecode.md) §1. Each row is
a *verifier* profile that selects one of the two bytecode
profiles. Numbers are deliberately conservative starting points
to be refined by measurement during prototyping.

| Verifier profile | Bytecode profile | Where it runs | Step cap | Max text | Stack | Sleepable | Recursion | RVV | Helpers |
| ---------------- | ---------------- | ------------- | -------- | -------- | ----- | --------- | --------- | --- | ------- |
| `linux-rv64/default`   | `merlin-linux-rv64` | Linux atomic hooks (XDP-V, TC-V, kprobe-V, …)                  | 1 M | 1 MB | 8 KB  | no  | bounded ≤ 8  | no  | small, eBPF-shaped |
| `linux-rv64/sleepable` | `merlin-linux-rv64` | Linux sleepable hooks (LSM-V, fentry-V on sleepable funcs, …) | 4 M | 4 MB | 64 KB | yes | bounded ≤ 32 | no  | broader, includes mem-alloc helper |
| `linux-rv64/largemem`  | `merlin-linux-rv64` | Linux long-running data-plane programs with `bpf_arena_v`     | 8 M | 4 MB | 64 KB | yes | bounded ≤ 32 | optional | above + arena ops |
| `offload/nic-firmware` | `merlin-linux-rv64` *or* `merlin-rtos-rv32` (vendor-selected) | RISC-V SmartNIC / DPU / accelerator firmware | per-vendor | per-vendor | per-vendor | yes | bounded ≤ 64 | yes (if device has it) | vendor-defined, large |
| `rtos-rv32/zephyr`     | `merlin-rtos-rv32` | Zephyr on RISC-V MCU (ESP32-C3, MPFS E51, FPGA soft cores)    | 8 K | 64 KB | 4 KB  | yes (cooperative) | bounded ≤ 16 | no (initially) | RTOS-shaped (timers, GPIO, logging) |
| `user-mode/sandbox` *(future)* | either | User-space sandbox process                                          | 8 M | 8 MB | 256 KB | yes | bounded ≤ 64 | yes | very broad, syscall-shaped |

The bytecode profile constrains the *march* the loader will
accept; the verifier profile constrains the *behaviour* the
verifier will tolerate. They compose: a `linux-rv64/sleepable`
verifier profile only accepts programs that declared the
`merlin-linux-rv64` bytecode profile *and* whose declared hook
permits sleepable behaviour.

Two design rules:

- A profile is a **relaxation set**, not a *replacement* abstract
  domain. The register-state, range, pointer-provenance, and
  liveness machinery is the same across profiles.
- A profile **never relaxes soundness**. It may relax precision, the
  helper allow list, or the maximum admissible program size, but
  the abstract domain must still prove no out-of-bounds access, no
  unbounded computation, and no illegal indirect control flow.

### 7.3 What relaxation actually changes

For each axis of relaxation, this is what changes in the verifier:

- **Sleepable / faultable.** Verifier accepts calls to sleepable
  helpers; relaxes the "no page-faulting load" rule; pre-emption
  bracketing handled by the runtime, not by the verifier.
- **Larger stack / text.** Step cap and bookkeeping memory grow
  proportionally; widening discipline unchanged.
- **Recursion.** Verifier proves a static depth bound per call site;
  the runtime enforces a stack high-water check on entry.
- **`A`-extension atomics.** Verifier requires the operand to be a
  properly typed shared-memory root (map value, arena slot, etc.);
  no raw atomic on uncategorised memory.
- **RVV.** Vector pointer arithmetic is verified by treating each
  active element as a separate typed pointer (a substantial verifier
  expansion; planned for Phase 2).
- **F/D.** Permitted only in user-mode and (optionally) offload
  profiles; never in Linux in-kernel paths.
- **Broader helper set.** Each helper is declared with a typed
  signature including may-sleep, may-fault, and side-effect class.
  Verifier checks per-call obligations against the program's profile.

### 7.4 What a profile MUST NOT do

A profile **must not**:

- Disable any soundness check. Profiles relax *what is permitted*,
  not *what is checked*.
- Allow privileged instructions (mret, sret, CSRs except as
  expressly enumerated by the profile and gated by the runtime).
- Allow arbitrary indirect jumps. Even the most permissive profile
  resolves indirect calls through a typed kfunc / helper table.
- Allow self-modifying code.
- Allow programs to mint pointers from integers. All pointers must
  derive from a typed root.

### 7.5 Profile negotiation at load

The loader (`MERLIN_PROG_LOAD`) checks:

1. The requested profile is implemented and enabled on this host.
2. The caller holds the capability associated with that profile.
3. The hook the program will attach to permits the profile (e.g.
   atomic XDP-V hook will not accept a `linux-rv64/sleepable`
   program).
4. The RISC-V ISA features the program uses are subsumed by the
   profile's permitted extension set.

Mismatch on any of these fails the load with a clear error.

### 7.6 Verifier implementation impact

To support this, the verifier is structured as:

```
struct merlin_profile {
    const char           *name;
    u32                   step_cap;
    u32                   max_text;
    u32                   max_stack;
    u32                   max_call_depth;
    bool                  sleepable_allowed;
    bool                  rvv_allowed;
    bool                  fp_allowed;
    bool                  amo_allowed;
    const struct merlin_helper_set  *helpers;
    const struct merlin_isa_subset  *isa;
    const struct merlin_widening    *widening;
};
```

The transfer functions and abstract domain do not look at the
profile directly; they read these knobs through accessor inlines.
This keeps the *checker* uniform and makes the profile choice an
authorisation / configuration decision rather than a logic-of-the-
verifier decision.

### 7.7 Implementation phasing

The verifier ships profile-aware from day one, even though only the
default profile is implemented:

- **Phase 1 (RFC v1):** `linux-rv64/default` only. The profile
  struct exists, but only one populated instance.
- **Phase 2:** add `linux-rv64/sleepable`, `rtos-rv32/zephyr`.
- **Phase 3:** `offload/nic-firmware` (requires the device control
  protocol from [07-jit-and-offload.md](07-jit-and-offload.md) §5).
- **Phase 4:** `linux-rv64/largemem` (depends on a MERLIN-V arena
  facility; coordinate with eBPF `bpf_arena` work).
- **Phase 5 (research):** `user-mode/sandbox`.

This sequencing keeps the RFC v1 narrow enough to be reviewable
while leaving the architecture honest about the rest of the design
space.

## 8. Open items

- Decide whether to verify before or after linker relaxation (some
  RISC-V toolchains relax sequences in-place; verifier must either
  understand both forms or require `-mno-relax`).
- Decide the `merlin_loop()` helper signature (or replace with a
  registered kfunc, leaning that way given the helper-ABI
  decision in [02-isa-and-bytecode.md](02-isa-and-bytecode.md) §6).
- Decide the policy for `A`-extension atomics in shared memory: allow
  any typed-pointer atomic, or require a helper for cross-CPU
  ordering critical sections. `A` is mandatory in `merlin-linux-rv64`
  but the verifier policy for raw AMOs is a separate question.
- Assign capability names to each non-default verifier profile
  (`CAP_MERLIN` for default; `CAP_MERLIN_OFFLOAD` for offload;
  `CAP_MERLIN_RTOS` for embedded; etc.). UAPI-impacting; coordinate
  with [03-kernel-interfaces.md](03-kernel-interfaces.md).
- Decide whether a profile's helper set is part of the kernel ABI
  (UAPI-stable) or per-program-type (looser).
- Decide whether the optional `user-mode/sandbox` profile lives in
  this project or in a sibling project; the answer affects whether
  the verifier ships in a usable user-space form.

The following previously-open items are now **decided** (recorded
here for traceability):

- ~~Choose Option A vs Option B for abstract-domain sharing with
  eBPF~~ → §4, decided: Option B for RFC v1; Option A as a
  follow-up.
- ~~Lock the initial profile catalogue~~ → §7.2, decided. Catalogue
  names aligned to the pinned bytecode profile names in
  [02-isa-and-bytecode.md](02-isa-and-bytecode.md) §1.
