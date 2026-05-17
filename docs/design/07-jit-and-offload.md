# 07 — JIT and Hardware Offload

Status: starter; **x86_64 host JIT prototype landed (tools/merlin-jit-x86_64/, see §8)**
Owner: PacketFive
Last reviewed: initial draft + first host JIT prototype

This document describes how a verified MERLIN-V image becomes executable
code on (a) RISC-V hosts and accelerators and (b) non-RISC-V hosts.

## 1. Two paths, one image

A JIT step exists for every verified MERLIN-V program — verified code is
never executed as bytecode. What differs is whether that step has to
*translate* the program or merely *install* it.

After the verifier accepts a program, the kernel chooses one of:

- **Pass-through path** (target is RISC-V, profile-compatible): the
  bytecode already *is* the host's native ISA. The "JIT" patches
  relocations, allocates executable pages, flushes I-cache, and jumps.
  No instruction translation — 1:1, hardware-native execution.
- **Host JIT path** (target is x86\_64, arm64, …): a real,
  per-architecture translator emits host code from every verified
  RISC-V instruction. Structurally similar to the per-architecture
  eBPF JITs in `bpf-next/arch/$ARCH/net/bpf_jit_*.c`; the input is the
  difference (stock RISC-V machine code, not eBPF bytecode).

For comparison, **eBPF's host path is always the second kind** — even
on a RISC-V host, eBPF goes through `arch/riscv/net/bpf_jit_comp.c` to
translate, because no commodity CPU implements eBPF's bytecode
natively. MERLIN-V eliminates that translation on RISC-V targets and
reuses (a stock-RISC-V-input version of) the same pattern everywhere
else.

A given MERLIN-V image can take either path without modification. The
choice is made by the runtime based on the host ISA and the program's
declared profile.

## 2. Pass-through path (RISC-V host or accelerator)

Steps:

1. Allocate an executable, RX-mapped region (`merlin_text_alloc()`),
   honouring kernel hardening (X^W, RELRO-like rules).
2. Copy the verified `.text` from the ELF.
3. Resolve relocations:
   - Standard `R_RISCV_*` PC-relative relocs against the program's
     own data sections.
   - `R_MERLIN_HELPER_ID` — patch the `li a7, ID` immediate at the call
     site.
   - `R_MERLIN_KFUNC_SLOT` — fill in the kfunc dispatch table entry.
   - `R_MERLIN_MAP_FD` — patch in the in-kernel pointer to the
     instantiated map.
   - `R_MERLIN_CORE_FIELD` — patch field offsets resolved against the
     running MERLIN BTF.
4. Emit `fence.i` (Zifencei) for I-cache coherence and the appropriate
   inner shareable IPI broadcast if SMP.
5. Mark the page read-only, executable.
6. Return the entry symbol address.

There is no instruction translation. On well-aligned RISC-V hosts the
entire load-to-run cost is dominated by page allocation and reloc
patching.

## 3. Host JIT path (non-RISC-V host)

This path is structurally similar to the per-architecture eBPF JITs
already in the tree (`bpf-next/arch/$ARCH/net/bpf_jit_*.c`). The
difference is the *input bytecode*: MERLIN-V's input is stock RISC-V
machine code (a defined profile of it), so the translator's decoder
is a standard RISC-V decoder rather than an eBPF decoder, and its
register file is RV's `x0..x31` rather than eBPF's `r0..r10`. The
output side — emitting host code, doing register allocation,
handling helper trampolines — looks much like the eBPF JITs.

The host JIT lives under `kernel/merlin/jit/arch/$HOST_ARCH.c`.

Design:

- Single-pass translator with a tiny peephole pass.
- Each verified `struct merlin_insn` (see [06-verifier.md](06-verifier.md))
  maps to a small fixed sequence of host instructions.
- The RISC-V register file maps 1:1 to a virtual register set; the
  translator's register allocator pins frequently-used virtual
  registers to host registers and spills the rest to a per-program
  scratch area in the kernel stack frame.

Architectural notes per host:

- **x86\_64.** Map RV `xN` to a `r12`-based virtual file with the busy
  set on `rbx, r12–r15` plus `rbp`. `ecall` → `call merlin_helper_trampoline`
  with `a7` materialised as the first call arg.
- **arm64.** Map RV `xN` to `x19–x28` for callee-saved virtuals plus
  scratch in `x9–x15`. `ecall` → `bl merlin_helper_trampoline`.

Additional non-RISC-V host JITs (ppc64le, s390x, loongarch, etc.)
are not in the project's plan: they belong to whichever future
contributor brings the corresponding hardware and the willingness to
maintain the backend.

The host JIT does *not* need to be byte-exact RVWMO-equivalent; it
needs to be observably equivalent under the MERLIN-V memory model, which
the verifier has already pinned down by tracking fences and atomics.

## 4. Compatibility with the kernel's existing eBPF JITs

`bpf-next/arch/riscv/net/bpf_jit_*.c` is an **eBPF → RISC-V** JIT.
That is the *opposite direction* from MERLIN-V's pass-through path
(MERLIN-V's input is already RISC-V) and from MERLIN-V's host JIT (MERLIN-V's
output is non-RISC-V). The two stacks coexist trivially because
their input bytecodes are different.

The one place to be careful: on a RISC-V host running both eBPF and
MERLIN-V, eBPF programs still go through `arch/riscv/net/bpf_jit_*` (no
change), and MERLIN-V programs go through `kernel/merlin/jit/pass_through.c`.
They share map infrastructure (§5 of [03-kernel-interfaces.md](03-kernel-interfaces.md))
but not text pages.

## 5. Hardware offload

The "1:1 offload" claim is realised as follows.

### 5.1 RISC-V SmartNICs and PCIe/CXL/UALink accelerators

1. Driver (`drivers/net/ethernet/$VENDOR/`) registers
   `ndo_bpf` / `ndo_merlin` capability.
2. On `MERLIN_LINK_CREATE` with offload mode, the kernel ships the
   *post-verifier, post-relocation* image to the device over its
   control channel.
3. Device firmware (the "MERLIN-V monitor") re-checks profile membership
   (defence in depth), maps the image into a U-mode-equivalent
   sandbox, and installs it on a fast-path core.
4. Maps are accessed via DMA-coherent regions or via a device-side
   helper that proxies back to the host.

There is no translation step. The device's RISC-V core executes the
same instructions the host's RISC-V core would have.

### 5.2 FPGA soft cores (PolarFire MiV / VexRiscv)

1. Bitstream synthesised with a small RV32IMC soft core.
2. Host loads the verified MERLIN-V image into a BRAM/DDR region
   reachable by the soft core.
3. A simple AXI mailbox kicks the soft core; the soft core executes
   the image.

This is the simplest concrete demonstration of the "no translation"
claim and is one of the headline experiments for the RFC.

### 5.3 Microcontroller targets (ESP32-C3)

The C3 is not an offload target in the SmartNIC sense — it is the
host. The MERLIN-V runtime on Zephyr loads a verified image into RAM,
flushes I-cache, and jumps. Same machinery as the kernel pass-through
path, just on a much smaller runtime.

## 6. Security boundary

- Pass-through path: the verifier is the only line of defence. There
  is no second-layer translation that could mask a bad accept. The
  verifier's safety properties (see [06-verifier.md](06-verifier.md))
  must therefore be airtight.
- Host JIT path: the JIT itself is in-kernel C; bugs there are
  classical kernel CVEs. The translator is small and is fuzzed against
  the verifier (any insn accepted by the verifier must be translatable).
- Offload path: the device-side monitor performs a profile
  re-validation before installing.

## 7. Open items

- Lock down the MERLIN-V-specific reloc type numbers (`R_MERLIN_*`).
- Decide whether the host JIT emits position-independent code (so we
  can support live-update / hot-patch of programs).
- Decide cache-line and TLB-shootdown discipline for SMP installs.
- Specify the device control protocol used by the offload path
  (mailbox layout, command set).
- Decide the policy on linker relaxation across the pass-through
  reloc patches.

## 8. v0 x86_64 host JIT prototype

A working user-space prototype lives under
[`../../tools/merlin-jit-x86_64/`](../../tools/merlin-jit-x86_64/).
It is the iteration vehicle for the §3 host JIT design; the in-kernel
JIT under `kernel/merlin/jit/arch/x86_64.c` will be a port of the
same algorithm.

### v0 strategy: spill-everything

The prototype skips register allocation entirely:

- All 32 RV registers live in a `uint64_t regs[32]` array on the
  JIT'd function's stack frame.
- `rbx` is pinned to point at `&regs[0]` for the function's
  lifetime (callee-saved per SysV ABI).
- Each RV instruction becomes "load operands from `regs[]`, do
  the op on `rax`/`rcx`, store back to `regs[]`".  Typical cost:
  4–8 x86 instructions per RV instruction.

This is intentionally slow but transparent — every RV register
access shows up at a fixed `[rbx+N*8]` displacement in the host
binary, making the emitted code easy to read.  Real register
allocation (pin frequently-accessed virtuals to `r12`–`r15` plus
`rbp`, spill the rest) is Phase 2 work.

### Helper-call convention

The prototype's `ecall` translation:

```
mov  rdi, rbx                       ; rdi = &regs[0]
mov  rax, <trampoline-fn-pointer>   ; absolute 64-bit address
call rax
```

The C trampoline reads `a7 = regs[17]`, dispatches on the helper
id, calls the helper with `a0..a5 = regs[10..15]`, and writes the
return value back to `regs[10]`.  Stub helpers in
`helpers.c` return recognisable sentinel values per ID; real
helper bodies will land in `kernel/merlin/helpers.c` when the
in-tree work begins.

### Instruction support in v0

Implemented: `addi` (add only), `lui`, `add`/`sub`/`or`/`and`/`xor`
(reg-reg), `mul`, `slli`/`srli`/`srai`, `ecall`, `jalr ra` (=
return), `fence` (no-op).  This is sufficient to JIT every
positive-test fixture the verifier accepts plus a handful of
arithmetic patterns.

Not yet implemented (Phase 2; mechanical to add): the rest of the
I-form ALU ops, all loads and stores, branches, `jal` non-return
form, RV64I word-form arithmetic (`addw`/`subw`/`sllw`/`srlw`/
`sraw`), division.

### End-to-end pipeline proof

The tool chain works end-to-end on the dev host:

```
.merlin.o  ->  merlin-objtool validate  ->  merlin-verifier  ->
   merlin-jit  ->  mmap PROT_EXEC  ->  call  ->  return value
```

The test battery (6 cases) asserts exact return values and helper-
call counts.  All six pass; see `tools/merlin-jit-x86_64/run-tests.sh`
for the executable spec.
