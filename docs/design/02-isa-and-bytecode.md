# 02 — BPF-V ISA and Bytecode

Status: profile pinned; helper ABI pinned
Owner: PacketFive
Last reviewed: profile and helper-ABI decisions landed

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

## 1. Profile names (decided)

A BPF-V program declares which *profile* it requires. Two profiles
are defined; a program names one of them in its `.bpfv.meta`
section (§8), and the loader rejects a program whose declared
profile is not enabled on the running execution engine.

| Profile name | march string | psABI | Where it runs |
| ------------ | ------------ | ----- | ------------- |
| **`bpfv-linux-rv64`**  | `rv64imac_zicsr_zifencei` | lp64  | Default Linux in-kernel; MPFS Icicle Kit Linux on U54; RV64 SmartNIC firmware. |
| **`bpfv-rtos-rv32`**   | `rv32imc_zicsr_zifencei` (optional `_a` for atomics) | ilp32 | Zephyr/RTOS; ESP32-C3-DevKitM-1; MPFS E51 monitor core; FPGA soft cores on PolarFire fabric. |

`zicsr` and `zifencei` appear in both `march` strings because the
*loader* uses CSRs and `fence.i` during install; the program
itself uses neither — see §4.

Future profiles (sleepable, largemem, offload/nic-firmware,
user-mode/sandbox) extend the relaxation set without changing the
base march strings; see [06-verifier.md](06-verifier.md) §7.

### 1.1 Why not adopt RVA20U64 / RVA22U64?

The RISC-V Application Profiles RVA20U64 and RVA22U64 **mandate
F and D** (single- and double-precision floating point). BPF-V
forbids FP in kernel paths. Adopting an RVA profile name while
disabling its mandatory extensions would be a misuse of the name.
The project therefore defines its own profile names and pins
their `march` strings explicitly, rather than inheriting and
violating an external profile.

## 2. Permitted base ISAs

The base ISA is `RV32I` (for `bpfv-rtos-rv32`) or `RV64I` (for
`bpfv-linux-rv64`). No mixing.

## 3. Extension policy (decided per profile)

The per-profile extension policy is pinned. Each row is one of
**M** (mandatory), **O** (optional, profile-controlled flag), or
**F** (forbidden). Where a profile says "M" the loader rejects an
image whose declared `march` does not include the extension;
where a profile says "F" the verifier rejects any instruction
belonging to it.

| Ext | `bpfv-linux-rv64` | `bpfv-rtos-rv32` | Rationale |
| --- | ----------------- | ---------------- | --------- |
| I (base)            | M | M | Required. |
| M (mul/div)         | M | M | Modern compilers assume it; div-by-zero handled by verifier. |
| A (atomics)         | M | O | Linux side has it everywhere we deploy; some embedded RV32 cores omit it. |
| C (compressed)      | M | M | Code density. Both ESP32-C3 (`rv32imc`) and MPFS support C. |
| F (float)           | F | F | No FP in kernel paths. |
| D (double)          | F | F | As F. |
| Zicsr               | F (programs)<br/>M (runtime) | F (programs)<br/>M (runtime) | Programs cannot read or write CSRs. Loader uses them. |
| Zifencei            | F (programs)<br/>M (runtime) | F (programs)<br/>M (runtime) | Programs do not emit `fence.i`. Loader emits one after copying text. |
| Zba, Zbb, Zbs       | O | O | Bit-manipulation, pure ALU, easy to verify; permitted if hardware provides. |
| Zicboz, Zicbom, Zicbop | F | F | Cache-block management belongs to the kernel, not BPF-V programs. |
| Ztso                | O | O | If hardware advertises Ztso, programs may rely on TSO; otherwise RVWMO with explicit fences. |
| V (vector)          | F (Phase 2) | F | Verifier complexity. Revisit post-RFC v1. |
| H (hypervisor), S (supervisor) | F | F | Privileged modes are never available to programs. |

The set of *forbidden* opcodes is a property of the verifier (see
[06-verifier.md](06-verifier.md)). Most of "forbid extension X"
reduces to "reject any instruction whose major+minor opcode
belongs to X."

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

### 6.2 Helper invocation (source-level encoding)

A program emits the following pattern at every helper-call site:

```
li    a7, HELPER_ID         // a7 = compile-time-constant helper id
mv    a0, ...               // a0..a5 = args 0..5
ecall                       // placeholder; rewritten by loader, see §6.4
// a0 = return value
```

The verifier ensures, at every `ecall` site:

- `a7` is a compile-time constant at the site.
- The helper id is in the program-type-permitted helper set.
- Argument types in `a0`–`a5` match the helper signature.

The `ecall` instruction in this pattern is **not** executed as
`ecall` at runtime. See §6.4 for how the loader transforms it.

### 6.3 kfunc invocation (typed kernel function references)

Kfuncs are dispatched via `jalr` through a reloc'd jump table,
exactly like a normal indirect call:

```
auipc t0, %pcrel_hi(kfunc_tbl)
ld    t0, %pcrel_lo(...)(t0)   // base of table  (lw for rv32)
ld    t0, KFUNC_IDX*8(t0)       // 8/4 for rv64/rv32
jalr  ra, t0, 0
```

A BPF-V relocation (`R_BPFV_KFUNC_SLOT`) marks the table slot
with the kfunc name; the loader resolves it against the running
kernel / firmware kfunc registry.

### 6.4 Loader rewrites at install time

**Decided design.** There is no `ecall` instruction in the
*running* image. The loader rewrites every `ecall` site at
install time, while the executable pages are still RW-mapped, in
both the RISC-V pass-through path and the host-JIT path.

The source-level `li a7, ID; ecall` is exactly 8 bytes (4 bytes
for `li` after constant materialisation, 4 bytes for `ecall`)
when `ID` fits in a 12-bit immediate, which is the only legal
case (helper ids are small). For larger ids the compiler emits
`lui + addi + ecall`; the verifier rejects helper ids that do
not fit the encoding the loader can rewrite.

**RISC-V pass-through path.** The loader replaces the 8-byte
`li a7, ID; ecall` sequence with an 8-byte direct call to the
helper trampoline:

```
auipc t0, %pcrel_hi(trampoline_for_ID)
jalr  ra, %pcrel_lo(...)(t0)
```

Helper trampolines live in a per-program-type table that the
runtime maintains; each trampoline is a few instructions that
set up the helper's typed signature and tail-call into the
helper's C implementation. Because `auipc + jalr` reaches
anywhere in the kernel's 32-bit PC-relative range, the trampoline
table does not have to live next to the program text.

**Host JIT path.** The host JIT translates each verified `ecall`
site into a host-native call into the helper trampoline,
materialising `a7`'s helper id at JIT time and emitting a single
`call rel32` (x86\_64) or `bl` (arm64) into the trampoline. No
indirect dispatch at runtime.

**Why ecall as the marker.** A literal `ecall` would trap to the
next-higher privilege mode on real hardware, which is wrong:
BPF-V programs already execute in kernel context in the
pass-through path. The rewrite at install time removes the
problem entirely. Using `ecall` as a marker is convenient
because (a) compilers emit it natively from a `syscall()`-shaped
intrinsic, (b) it is a single 4-byte instruction with a clear
encoding, and (c) any unintended path that fails to rewrite
the site fails loudly with an illegal-instruction-from-kernel
trap rather than silent corruption.

**Verifier obligation.** The verifier must prove the program
contains no executable `ecall` instruction *not* preceded by an
`li a7, CONST` that the verifier could analyse. The loader's
rewrite is then well-defined for every reachable `ecall` site
in the verified image.

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

Compiles (with stock `riscv64-unknown-linux-gnu-gcc -O2
-ffreestanding -mcmodel=medany -mabi=lp64
-march=rv64imac_zicsr_zifencei -nostdlib`) to roughly:

```asm
drop_all:
    li      a0, 1          # XDP_V_DROP
    ret
```

— two instructions, no helpers, no relocations. On a RISC-V host this
loads at native speed; on x86\_64 the host JIT emits two x86
instructions. There is no eBPF-style decode step.

## 10. Open items

- Decide whether `A`-extension AMOs are permitted directly in
  programs or only via helpers (independent of profile membership;
  `A` is mandatory on `bpfv-linux-rv64` but the verifier policy
  for raw AMOs is a separate question).
- Decide whether vector (`V`) becomes a per-program flag inside
  `bpfv-linux-rv64` post-RFC, or a new profile name
  (`bpfv-linux-rv64v`). Out of scope for RFC v1.
- Define exact `.bpfv.*` section binary layouts (see
  [03-kernel-interfaces.md](03-kernel-interfaces.md)) — work
  unblocked by Decisions 1 and 2.
- Define BPF-V-specific ELF reloc type numbers (`R_BPFV_*`).
- Decide policy on linker relaxation: verify before or after
  relaxation, or require `-mno-relax` from the compiler. See
  also [06-verifier.md](06-verifier.md) §8.

The following previously-open items are now **decided** (recorded
here for traceability):

- ~~Pin a specific RISC-V profile~~ → §1, decided: two project
  profiles, `bpfv-linux-rv64` and `bpfv-rtos-rv32`.
- ~~Decide compressed-instruction policy~~ → §3, decided: `C` is
  mandatory in both profiles.
- ~~Helper invocation: `ecall` with `a7`, or jump-table~~ → §6.2
  and §6.4, decided: `ecall` source-level encoding, loader
  rewrites to direct call at install time.
