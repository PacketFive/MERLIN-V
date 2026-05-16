# 01 — eBPF Comparative Analysis

Status: starter
Owner: PacketFive
Last reviewed: initial draft

This document records what eBPF is, factually, and where MERLIN-V chooses
to diverge. It is the input to the "Related work" / "Background"
section of the eventual RFC and paper.

> Note on attribution: eBPF is upstream in the Linux kernel and is
> maintained by the kernel BPF subsystem maintainers (see the
> `BPF (SAFE DYNAMIC PROGRAMS AND TOOLS)` section of `net-next/MAINTAINERS`
> and the `bpf-next/MAINTAINERS` file). Major out-of-tree consumers
> (Cilium/Isovalent, Meta, Google, Microsoft, Red Hat, etc.) contribute
> heavily and employ many maintainers, but no single company "owns"
> eBPF — it is governed by the Linux kernel community and the eBPF
> Foundation. See `bpf-next/Documentation/bpf/` and the kernel's
> [BPF documentation index](https://docs.kernel.org/bpf/index.html).

## 1. eBPF anatomy (cheat sheet)

Authoritative references in this tree:

- `bpf-next/Documentation/bpf/` — full BPF documentation tree.
- `bpf-next/include/uapi/linux/bpf.h` — UAPI: syscall, program types,
  attach types, map types, helpers.
- `bpf-next/kernel/bpf/` — verifier, core dispatch, maps, JIT glue.
- `bpf-next/arch/*/net/bpf_jit*` — per-architecture JIT backends
  (including `arch/riscv/net/bpf_jit*` — eBPF JIT *targeting* RISC-V,
  which is conceptually adjacent but architecturally orthogonal to
  MERLIN-V).
- `bpf-next/tools/lib/bpf/` — libbpf.

### 1.1 ISA shape

- 11 general-purpose 64-bit registers: `r0`–`r10`, where `r10` is a
  read-only frame pointer.
- Fixed 8-byte instruction encoding (or 16 bytes for the wide-immediate
  `BPF_LD_IMM64`). Opcode classes: ALU/ALU64, JMP/JMP32, LD/LDX/ST/STX.
- No general indirect memory access; only loads/stores from typed,
  verifier-tracked pointers.
- Helper calls via `BPF_CALL` with a small numeric ID; tail calls via
  `bpf_tail_call`.
- 32-bit subregister semantics with explicit zero-extension on writes.
- A separate, limited "BPF assembly" mental model — there is no general
  RISC-V/x86-style instruction stream.

### 1.2 Verifier

- Performs symbolic abstract interpretation over the instruction
  stream.
- Tracks register types, ranges, alignment, pointer provenance, and
  liveness.
- Rejects unbounded loops (now relaxed via `bpf_loop` / bounded loop
  pragma), uninitialised reads, and out-of-bounds memory accesses.
- Enforces ABI rules (helper signatures, return-value handling,
  spill/fill discipline on stack).
- Tied to BTF (`bpf-next/kernel/bpf/btf.c`) for type info and to CO-RE
  relocations for portability.

### 1.3 JIT and interpreter

- Per-arch JIT under `arch/$ARCH/net/bpf_jit*.c`.
- Falls back to a C interpreter (`bpf-next/kernel/bpf/core.c`) when
  JIT is unavailable or disabled.
- A few archs (s390, mips32) historically lagged or lacked JITs;
  RISC-V has had a full eBPF JIT since v5.12 (rv64) / later for rv32.

### 1.4 Maps, helpers, link, BTF, CO-RE

- Maps: kernel objects implementing `struct bpf_map_ops`. Many types
  (hash, array, lru, lpm-trie, ringbuf, perf, sockmap, …). FD-based
  user/program handle.
- Helpers: numbered functions registered per program type; signatures
  declared in `bpf_helper_defs.h`.
- Kfuncs: typed kernel function references (post-helpers, more
  flexible).
- Link: an attachment handle (anchors a program to a hook with explicit
  lifetime).
- BTF: type info embedded in kernel and program objects.
- CO-RE: compile-once-run-everywhere; uses BTF + relocations to adapt
  field offsets across kernel versions.

### 1.5 Hardware offload (today)

- Netronome NFP is the canonical eBPF-offload-capable NIC. The model is:
  kernel verifier accepts the program, then `dev_ops->ndo_bpf` hands the
  program to the NIC driver, which compiles it for the NFP's
  microengines.
- This is a re-translation step: eBPF bytecode → NFP microcode. There
  is no native eBPF silicon at production scale.

## 2. Side-by-side: eBPF vs MERLIN-V

| Dimension | eBPF | MERLIN-V |
| --------- | ---- | ----- |
| Bytecode ISA | Custom 64-bit RISC-like (11 GPRs, 8-byte insns) | Restricted RV32I[MC] / RV64I[MC] profile (native RISC-V machine code) |
| Register file | r0–r10 (r10 fp, ro) | RV ABI: x0..x31 with verifier-imposed conventions on x1/x2/x8/x10–x17 |
| Calling convention | BPF helpers via `BPF_CALL` opcode | RISC-V `ecall` for helpers (a7 = helper id, a0–a6 = args), `jalr` for kfunc dispatch via reloc table |
| Compiler backend | LLVM BPF target, GCC `gcc-bpf` | Stock LLVM `riscv32`/`riscv64`, stock GCC `riscv*-gcc` (no new backend) |
| Verifier scope | Whole ISA, ~well-suited (small ISA, no general indirect) | RISC-V subset profile + abstract interpretation; complexity discussed in [06-verifier.md](06-verifier.md) |
| JIT cost on host | Always translate to host ISA — including on RISC-V hosts, because no silicon implements eBPF natively | RISC-V hosts: pass-through (no instruction translation); non-RISC-V hosts: real JIT, structurally similar to eBPF's per-arch JITs but with stock RISC-V machine code as input |
| HW offload on RISC-V NIC | Re-translate eBPF → NIC ISA | None: load image and run |
| Memory model | Sequential at insn granularity, atomics via dedicated helpers | RVWMO baseline; `A` extension optional; fences via `fence`/`fence.i`, profile-controlled |
| Type info | BTF | MERLIN BTF (BTF + RISC-V reloc kinds), see [04-toolchain.md](04-toolchain.md) |
| Portability mechanism | CO-RE | CO-RE-V (same idea, RISC-V relocs) |
| Userland library | libbpf | libmerlin (analogous; may share BTF infra) |
| OS reach | Linux, Windows (eBPF-for-Windows) | Linux, Zephyr, bare-metal accelerator firmware |
| License model | Kernel pieces GPL-2.0; libbpf dual GPL/BSD | Same |

## 3. What we *deliberately keep* from eBPF

- The conceptual split: loader → verifier → JIT → execution engine →
  map subsystem → helper / kfunc surface.
- BTF-style embedded type information.
- The CO-RE pattern of compile-once, relocate-on-load.
- Per-program-type hook contracts (XDP, TC, kprobe, tracepoint, …).
- The "no floats in kernel paths" rule, even though RV `F`/`D` exists.
- Capability-gated load (`CAP_BPF`-style) with strict default-deny.

## 4. What we deliberately drop or replace

- The custom ISA. RISC-V replaces it entirely.
- The custom LLVM/GCC BPF backend(s). Stock RISC-V toolchains replace
  them.
- The "always JIT-translate" runtime cost on RISC-V hardware.
- The need for vendor-specific re-translation on RISC-V SmartNICs and
  RISC-V CXL/UALink accelerators.

## 5. Open comparison work

These are the targeted measurements for the RFC/paper. Note the
explicit null hypothesis: not every measurement is expected to show
a MERLIN-V win, and we will report negative results as such (see
[`00-overview.md`](00-overview.md) §8.1).

1. **Verifier cost** on equivalent programs (eBPF vs MERLIN-V profile).
   Hypothesis: comparable; MERLIN-V may pay a small constant for a
   richer decoder, recovered by simpler typing rules.
2. **Load-to-run latency**: bytecode load → first instruction
   executed, on:
   - Linux / x86\_64 — real JIT for both. Hypothesis: comparable.
   - Linux / RISC-V — eBPF JIT vs MERLIN-V pass-through. Hypothesis:
     MERLIN-V substantially lower (no instruction translation).
   - RISC-V SmartNIC offload path. Hypothesis: MERLIN-V substantially
     lower (no re-translation to vendor microcode).
3. **Steady-state per-packet cycles** on XDP-equivalent workloads:
   - x86\_64 host, eBPF vs MERLIN-V. **Hypothesis: null result.**
     Reported deliberately to set the record straight — MERLIN-V's
     wins on commodity hosts are not at the per-packet steady-state
     level (see [`00-overview.md`](00-overview.md) §8).
   - RISC-V host, eBPF vs MERLIN-V. Hypothesis: comparable, with MERLIN-V
     having a load-time advantage that does not affect steady state.
   - RISC-V NIC offload vs host eBPF. Hypothesis: MERLIN-V offload
     substantially faster.
4. **Code size** of the verifier + JIT in MERLIN-V vs eBPF + per-arch
   JITs. Hypothesis: initially larger (parallel stacks); converges
   if Option A in [`06-verifier.md`](06-verifier.md) §4 succeeds.
5. **Toolchain footprint**: lines of code, build time, and bug-fix
   latency in the eBPF LLVM target and `gcc-bpf` backend over the
   last five years, compared to the cost of the MERLIN-V project's
   header set and minor pahole work. Static measurement, no
   prototype required. Hypothesis: substantial MERLIN-V advantage.
6. **Energy per packet** on the Icicle Kit (MPFS) and ESP32-C3
   (rv32imc) reference targets. Hypothesis: MERLIN-V demonstrates
   feasibility in regimes where eBPF was never deployed.

The methodology for each will be specified in a follow-up
`08-evaluation-plan.md` once the prototype runs end-to-end.

> Why measurement (3) deserves a "deliberate null result" framing:
> a non-trivial part of the RFC audience will assume the project
> claims runtime-performance superiority on x86\_64. We do not, and
> we want the measurement that proves it on the record. Hand-waving
> a performance claim we cannot defend would damage the legitimate
> claims (1, 2, 5, 6) by association.
