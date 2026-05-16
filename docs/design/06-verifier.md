# 06 — Verifier Strategy

Status: starter
Owner: PacketFive
Last reviewed: initial draft

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

The BPF-V verifier therefore combines two ideas:

1. **ISA-profile restrictions** that bring RISC-V closer to a
   verifiable subset (§2 of [02-isa-and-bytecode.md](02-isa-and-bytecode.md)).
2. **Abstract interpretation** over the resulting profile, tracking
   the same things eBPF's verifier tracks (register types, ranges,
   alignment, pointer provenance, liveness), driven by BTF-V (§3 of
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
     use the project-supplied `bpfv_loop()` helper.
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
   provable, or the program opts into `bpfv_loop()` with an explicit
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
profile; on `bpfv-rv32imc-ilp32` it is sharply tighter to keep
verification feasible on the loader CPU.

## 4. Sharing with the eBPF verifier

The two verifiers want the same *abstract domain* (tnum/range/pointer
provenance machinery) over different *concrete ISAs*. There are two
plausible factorings:

- **Option A — extract a shared `lib/bpf_absint/` module** from
  `kernel/bpf/verifier.c`, parameterise the per-insn transfer
  functions. eBPF and BPF-V each plug in their decoder + transfer
  function table.
- **Option B — independent verifier.** Simpler to land standalone, at
  the cost of code duplication that maintainers will reasonably push
  back on.

Initial preference: prototype as Option B (standalone, in
`kernel/bpfv/verifier.c`) to keep the RFC self-contained, but design
the data structures so a refactor to Option A is a syntactic
change. The RFC cover letter should explicitly invite kernel BPF
maintainers to choose A or B.

## 5. Decoder

The verifier decodes RISC-V instructions itself rather than calling
out to a disassembler. The decoder lives in `kernel/bpfv/decode.c`
and is generated from a machine-readable opcode table (likely
`riscv-opcodes` upstream, post-processed). The decoder produces a
small structured representation:

```c
struct bpfv_insn {
    enum bpfv_op   op;       // ADD, ADDI, LD, JAL, JALR, ECALL, ...
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
dialect, a BPF-V program could be an arbitrary application running
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

A program declares its profile in `.bpfv.meta` (see
[02-isa-and-bytecode.md](02-isa-and-bytecode.md) §8). The loader
selects the verifier profile to run accordingly, capability-gated by
profile (different profiles may require different capabilities, e.g.
`CAP_BPFV` for default in-kernel; `CAP_BPFV_OFFLOAD` for offload to a
trusted device; `CAP_BPFV_RTOS` for embedded; etc.). Default-deny
applies: an unknown profile is rejected.

### 7.2 Initial profile catalogue

The catalogue below is the *initial* design target. Numbers are
deliberately conservative starting points to be refined by
measurement during prototyping.

| Profile | Where it runs | Step cap | Max text | Stack | Sleepable | Recursion | RVV | Helpers |
| ------- | ------------- | -------- | -------- | ----- | --------- | --------- | --- | ------- |
| `linux-kernel/default` | Linux atomic hooks (XDP-V, TC-V, kprobe-V, …) | 1 M | 1 MB | 8 KB | no | bounded ≤ 8 | no | small, eBPF-shaped |
| `linux-kernel/sleepable` | Linux sleepable hooks (LSM-V, fentry-V on sleepable funcs, …) | 4 M | 4 MB | 64 KB | yes | bounded ≤ 32 | no | broader, includes mem-alloc helper |
| `linux-kernel/largemem` | Linux long-running data-plane programs with `bpf_arena_v` access | 8 M | 4 MB | 64 KB | yes | bounded ≤ 32 | optional | as above + arena ops |
| `offload/nic-firmware` | RISC-V SmartNIC / DPU / accelerator firmware | per-vendor | per-vendor | per-vendor | yes | bounded ≤ 64 | yes (if device has it) | vendor-defined, large |
| `rtos/zephyr` | Zephyr on RISC-V MCU | 8 K | 64 KB | 4 KB | yes (cooperative) | bounded ≤ 16 | no (initially) | RTOS-shaped (timers, GPIO, logging) |
| `user-mode/sandbox` (optional) | User-space sandbox process | 8 M | 8 MB | 256 KB | yes | bounded ≤ 64 | yes | very broad, syscall-shaped |

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

The loader (`BPFV_PROG_LOAD`) checks:

1. The requested profile is implemented and enabled on this host.
2. The caller holds the capability associated with that profile.
3. The hook the program will attach to permits the profile (e.g.
   atomic XDP-V hook will not accept a `linux-kernel/sleepable`
   program).
4. The RISC-V ISA features the program uses are subsumed by the
   profile's permitted extension set.

Mismatch on any of these fails the load with a clear error.

### 7.6 Verifier implementation impact

To support this, the verifier is structured as:

```
struct bpfv_profile {
    const char           *name;
    u32                   step_cap;
    u32                   max_text;
    u32                   max_stack;
    u32                   max_call_depth;
    bool                  sleepable_allowed;
    bool                  rvv_allowed;
    bool                  fp_allowed;
    bool                  amo_allowed;
    const struct bpfv_helper_set  *helpers;
    const struct bpfv_isa_subset  *isa;
    const struct bpfv_widening    *widening;
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

- **Phase 1 (RFC v1):** `linux-kernel/default` only. The profile
  struct exists, but only one populated instance.
- **Phase 2:** add `linux-kernel/sleepable`, `rtos/zephyr`.
- **Phase 3:** `offload/nic-firmware` (requires the device control
  protocol from [07-jit-and-offload.md](07-jit-and-offload.md) §5).
- **Phase 4:** `linux-kernel/largemem` (depends on a BPF-V arena
  facility; coordinate with eBPF `bpf_arena` work).
- **Phase 5 (research):** `user-mode/sandbox`.

This sequencing keeps the RFC v1 narrow enough to be reviewable
while leaving the architecture honest about the rest of the design
space.

## 8. Open items

- Choose Option A vs Option B for abstract-domain sharing with eBPF.
- Decide whether to verify before or after linker relaxation (some
  RISC-V toolchains relax sequences in-place; verifier must either
  understand both forms or require `-mno-relax`).
- Decide the `bpfv_loop()` helper signature.
- Decide the policy for `A`-extension atomics in shared memory: allow
  any typed-pointer atomic, or require a helper for cross-CPU
  ordering critical sections.
- Lock the initial profile catalogue (§7.2) and assign capability
  names to each non-default profile.
- Decide whether a profile's helper set is part of the kernel ABI
  (UAPI-stable) or per-program-type (looser).
- Decide whether the optional `user-mode/sandbox` profile lives in
  this project or in a sibling project; the answer affects whether
  the verifier ships in a usable user-space form.
