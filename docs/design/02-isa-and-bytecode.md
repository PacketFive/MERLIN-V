# 02 — BPF-V ISA and Bytecode

Status: starter
Owner: PacketFive
Last reviewed: initial draft

This document defines the BPF-V *bytecode* — which is to say, the
profile of the RISC-V ISA that a BPF-V program is permitted to use, and
the ABI rules layered on top of it.

References:
- *The RISC-V Instruction Set Manual, Volume I: User-Level ISA* and
  *Volume II: Privileged Architecture* (current ratified versions).
- *RISC-V ABIs Specification* (psABI), ilp32 and lp64 variants.
- *RISC-V Profiles* (RVA20, RVA22, RVA23, RVB23, …).
- `net-next/arch/riscv/include/asm/`, `net-next/Documentation/arch/riscv/`.
- `bpf-next/arch/riscv/net/bpf_jit_*.[ch]` (eBPF-on-RV reference).

## 1. Naming

A BPF-V program declares which *profile* it requires. A profile name is
of the form:

```
bpfv-<base>-<ext-bits>-<abi>
```

Examples used throughout this doc:

- `bpfv-rv32imc-ilp32` — embedded (Zephyr/ESP32-C3, NIC microengines).
- `bpfv-rv64imac-lp64` — server-class baseline.
- `bpfv-rv64gc-lp64d` — server-class with F/D enabled (user-space only,
  forbidden for in-kernel programs in the default policy).

A loader rejects a program whose declared profile is not enabled on the
running execution engine. See [03-kernel-interfaces.md](03-kernel-interfaces.md)
for the syscall surface and capability rules.

## 2. Permitted base ISAs

The base ISA is `RV32I` or `RV64I`. The base alone is enough to express
the BPF-V control flow and pointer arithmetic, but performance-relevant
deployments will pin a richer profile (M, A, C, optionally Zba/Zbb).

| Base | When |
| ---- | ---- |
| RV32I | Microcontroller targets (ESP32-C3 is rv32imc), 32-bit NIC microengines, FPGA soft cores when area-constrained. |
| RV64I | Linux hosts on RISC-V, MPFS (rv64imafdc) — though F/D will typically be disabled by policy in the in-kernel profile. |

## 3. Extension policy (initial proposal)

Status legend: **M**andatory, **O**ptional, **F**orbidden in the
default in-kernel profile.

| Ext | Status (in-kernel default) | Status (user-space / NIC firmware) | Rationale |
| --- | -------------------------- | ---------------------------------- | --------- |
| I (base) | M | M | Required. |
| M (mul/div) | M | M | Modern compilers assume it; trap on div-by-zero handled by verifier. |
| A (atomics) | O | O | Permitted only via verifier-approved helper-implemented patterns; raw `lr/sc` and AMOs gated by profile. |
| F (float) | F | O | No FP in kernel. User-space accelerators may enable; F/D never imply context-switch cost on the host. |
| D (double) | F | O | As F. |
| C (compressed) | O | O | Smaller code; verifier handles 2-byte instruction alignment. |
| Zicsr | F | F (default) | CSR access reserved to the runtime, not to programs. |
| Zifencei | M | M | Required so the JIT/loader can publish the I-cache flush. Programs themselves do not emit `fence.i`. |
| Zba, Zbb, Zbs | O | O | Bit-manipulation; pure ALU, easy to verify. |
| Zicboz, Zicbom, Zicbop | F | O | Cache-block management — kernel-only or accelerator-firmware-only. |
| Ztso | O | O | If present, profile pins TSO; otherwise WMO with explicit fences. |
| V (vector) | F (initial) | O | Phase 2 work; verifier complexity is substantial. |
| H (hypervisor), S (supervisor) | F | F | Privileged modes are never available to programs. |

The set of *forbidden* opcodes is a property of the verifier (see
[06-verifier.md](06-verifier.md)). Most of "forbid extension X" reduces
to "reject any instruction whose major+minor opcode belongs to X."

## 4. Forbidden / restricted instructions (even within enabled extensions)

Independent of extension selection, the following are always rejected:

- All privileged instructions: `mret`, `sret`, `wfi`, `sfence.vma`,
  `hfence.*`, ...
- All CSR instructions (`csrr*`, `csrw*`).
- `ecall` *except* with `a7` set to a registered BPF-V helper number
  (see §6 ABI). The verifier must prove `a7` is a compile-time constant
  helper id at every `ecall` site.
- `ebreak` — replaced by a verifier-emitted trap helper if the program
  needs `assert()`-like semantics.
- `fence.i` from program code; only the loader emits it.
- Indirect jumps (`jalr`) targeting anything other than:
  - a verified function pointer derived from a kfunc reloc, or
  - the return address pushed by a verified call sequence.

## 5. Register usage

Standard RISC-V ABI registers carry the following BPF-V meaning:

| Reg | psABI role | BPF-V role |
| --- | ---------- | ---------- |
| x0 (`zero`) | hardwired zero | same |
| x1 (`ra`) | return address | same; verifier tracks call depth |
| x2 (`sp`) | stack pointer | same; bounded program stack, size declared in meta section |
| x3 (`gp`) | global pointer | reserved by runtime; program does not touch |
| x4 (`tp`) | thread pointer | reserved by runtime |
| x5–x7 (`t0`–`t2`) | temps | general-purpose, caller-saved |
| x8 (`s0`/`fp`) | frame pointer | frame pointer, verifier-tracked |
| x9 (`s1`) | saved | general-purpose, callee-saved |
| x10–x11 (`a0`–`a1`) | args / return | helper args 0–1, helper return in a0 |
| x12–x17 (`a2`–`a7`) | args | helper args 2–7 (a7 doubles as helper id at `ecall`) |
| x18–x27 (`s2`–`s11`) | saved | general-purpose, callee-saved |
| x28–x31 (`t3`–`t6`) | temps | general-purpose, caller-saved |

`a0`–`a5` carry the first 6 helper arguments (matching psABI), `a7`
carries the helper id at the `ecall` site. `a0` carries the helper
return value.

## 6. ABI: program entry, helpers, kfuncs

### 6.1 Program entry

The program's entry is a standard RISC-V function call:

```
ret = prog(ctx)              // a0 = ctx pointer, return in a0
```

`ctx` is a program-type-specific structure (e.g. `struct xdp_md_v`).
The verifier knows the type from BTF-V and tracks every field access.

### 6.2 Helper invocation

```
li    a7, HELPER_ID         // compile-time constant
mv    a0, ...               // args 0..5
ecall
// a0 = return value
```

The runtime traps `ecall`, dispatches by `a7` to the helper table for
the current program type, and returns. The verifier ensures:

- `a7` is a compile-time constant at every `ecall` site.
- The helper id is in the program-type-permitted helper set.
- Argument types in `a0`–`a5` match the helper signature.

### 6.3 kfunc invocation (typed kernel function references)

Kfuncs are dispatched via `jalr` through a reloc'd jump table, exactly
like a normal indirect call:

```
auipc t0, %pcrel_hi(kfunc_tbl)
ld    t0, %pcrel_lo(...)(t0)   // base of table
ld    t0, KFUNC_IDX*8(t0)
jalr  ra, t0, 0
```

A BPF-V relocation marks the table slot with the kfunc name; the loader
resolves it against the running kernel/firmware kfunc registry.

## 7. Memory model

Default profile is **RVWMO** (RISC-V Weak Memory Ordering). Programs
that need stricter ordering must:

- Use explicit `fence` instructions, *or*
- Declare a profile with `Ztso` if available on the target.

Atomics use the `A` extension when enabled. The verifier requires that
every shared-memory atomic operate on a properly typed pointer (map
value, ringbuf slot, etc.).

## 8. Object file format

A BPF-V object is a plain RISC-V ELF (`EM_RISCV`) with these
additions:

- `.bpfv.meta` — fixed-layout header: profile name, program type,
  required maps, kfuncs, license string, BTF-V offset.
- `.BTF.v` — BTF-V type info (see [04-toolchain.md](04-toolchain.md)).
- `.bpfv.maps` — map descriptor records (name, type, key/value
  size/types, max\_entries, flags).
- `.bpfv.relocs` — BPF-V-specific reloc kinds (helper id binding,
  kfunc binding, CO-RE-V field offset, map fd patch site).
- Standard RISC-V relocs (`R_RISCV_*`) are honoured by the loader for
  PC-relative addressing within the program.

A small ELF tool, `bpfv-objtool`, validates these sections before
upload. See [04-toolchain.md](04-toolchain.md).

## 9. Worked micro-example

A minimal "drop everything" XDP-V program (conceptual, RV64I):

```c
#include <bpfv/xdp.h>

SEC("xdp")
int drop_all(struct xdp_md_v *ctx)
{
    return XDP_V_DROP;
}
```

Compiles (with stock `riscv64-unknown-linux-gnu-gcc -O2 -ffreestanding
-mcmodel=medany -mabi=lp64 -march=rv64imac -nostdlib`) to roughly:

```asm
drop_all:
    li      a0, 1          # XDP_V_DROP
    ret
```

— two instructions, no helpers, no relocations. On a RISC-V host this
loads at native speed; on x86\_64 the host JIT emits two x86
instructions. There is no eBPF-style decode step.

## 10. Open items

- Pin a specific RISC-V *profile* (RVA20U64? a BPF-V-defined
  `bpfv-rva20u64-strict`?) and publish a conformance test suite.
- Decide compressed-instruction policy: allowed but the verifier
  expands them mentally, or forbidden so the verifier is simpler?
- Decide whether `A`-extension AMOs are permitted directly or only via
  helpers.
- Decide whether vector (V) is "phase 2" or "never in kernel".
- Define exact `.bpfv.*` section binary layouts (see
  [03-kernel-interfaces.md](03-kernel-interfaces.md)).
- Define BPF-V-specific ELF reloc type numbers.
