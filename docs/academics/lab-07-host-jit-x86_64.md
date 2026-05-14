# Lab 07 — Host JIT for x86\_64

Module: 4
Effort tier: L
Prerequisites: Lab 06.
Design doc: [`docs/design/07-jit-and-offload.md`](../design/07-jit-and-offload.md) §3.

## Learning objectives

- Implement a single-pass, table-driven instruction translator.
- Choose a register allocation strategy under tight constraints.
- Generate position-independent x86\_64 code from RISC-V input.
- Reuse the Lab 04 verifier's output as the JIT's input contract.

## Background reading

- *Intel 64 and IA-32 Architectures Software Developer's Manual*,
  Vol 2 (instruction set reference). You will not read all of it;
  you'll use it as a lookup.
- `bpf-next/arch/x86/net/bpf_jit_comp.c` — the canonical single-pass
  eBPF → x86\_64 JIT. Read this for *style*, not for copy-paste.
- Lab 06's source — your input format and verifier hooks.

## Specification

You will build `bpfvi-jit-x86_64`, which takes a verified BPF-V ELF and
emits x86\_64 machine code that, when called, produces the same
observable behaviour the program would have on a RISC-V host.

### Register mapping

| RV32 reg | x86\_64 reg | Role |
| -------- | ----------- | ---- |
| `x0`     | (none)      | hardwired zero, materialised on demand |
| `x1` (ra)| `r12`       | callee-saved on entry |
| `x2` (sp)| `rbp`       | program frame pointer; you reserve a real stack region |
| `x5–x7`  | `r13,r14,r15` | hot temps |
| `a0–a5`  | `rdi,rsi,rdx,rcx,r8,r9` | helper arg pass-through, x86 ABI |
| `a7`     | spill slot  | helper id at `ecall` sites |
| others   | spill slots in a per-program frame |

`x[0]` is *never* allocated; reads of `x0` are translated to
`xor eax, eax` (or the appropriate immediate-zero pattern).

### Translation table

For each RISC-V instruction in the verified profile, the JIT has a
small function emitting x86 bytes. Examples:

- `add rd, rs1, rs2`:
  - `mov  rax, [rs1_slot]`
  - `add  rax, [rs2_slot]`
  - `mov  [rd_slot], rax`
- `addi rd, rs1, imm`:
  - `mov  rax, [rs1_slot]`
  - `add  rax, imm`
  - `mov  [rd_slot], rax`
- `lw rd, off(rs1)`:
  - `mov  rax, [rs1_slot]`
  - `movsxd rcx, dword ptr [rax + off]`
  - `mov  [rd_slot], rcx`
- `beq rs1, rs2, target`:
  - `mov  rax, [rs1_slot]`
  - `cmp  rax, [rs2_slot]`
  - `je   target_label`
- `ecall`:
  - load `a7` slot to `rax`
  - `mov  rdi, [a0_slot]; mov rsi, [a1_slot]; ...`
  - `call bpfvi_helper_dispatch` (passes `rax` as helper id via
    a calling convention you define)

A small peephole pass merges adjacent `mov reg, mem; op` into
direct memory operands where the verifier's liveness analysis says
the intermediate is dead.

### Trampoline

You ship a `bpfvi_helper_dispatch` trampoline in C that:
1. Reads the helper id from the agreed register/slot.
2. Dispatches to the helper table from Lab 03.
3. Returns the value in `rax` (mapped to `a0` slot on return).

## Tasks

### Task 1 — Code emitter

Implement `src/emit.c`. Provide low-level emitters for:
- mov reg, imm32 / imm64
- mov reg, mem and mem, reg
- add / sub / and / or / xor / shl / shr / sar (reg-reg, reg-imm)
- cmp / je / jne / jl / jge (signed and unsigned variants)
- call rel32, ret
- jmp rel32

Unit-test by emitting known sequences and comparing to `nasm` output.

### Task 2 — Slot allocator

A per-program stack frame for spill slots:
- `[rbp - 8*0]` = `x[1]`
- `[rbp - 8*1]` = `x[5]`
- …

Function entry: `push rbp; mov rbp, rsp; sub rsp, N*8`.
Function exit: `mov rsp, rbp; pop rbp; ret`.

### Task 3 — Per-instruction translator

`src/translate.c` walks Lab 04's decoded `bpfvi_insn` stream and
calls the right emitter. Maintain a `pc → host_addr` map for branch
fixups.

### Task 4 — Branch fixup pass

After the first pass, walk the deferred branch list and patch the
rel32 displacement. Reject anything that ends up out-of-range (won't
happen for our tiny programs but reject cleanly anyway).

### Task 5 — End-to-end

Run on the same `tests/good/` programs as Lab 06. The host runs them
on x86\_64. Return values must match the RISC-V pass-through path.

### Task 6 — Benchmark

Report mean cycles per invocation on x86\_64 and compare with Lab 06's
RISC-V pass-through numbers (qualitatively — they're different
hardware). Note where the differences come from.

## Deliverables

- All source.
- A passing autograder log on x86\_64.
- `WRITEUP.md`:
  - Where does your translator *do less* than a real optimizing
    compiler, and why does that matter for BPF-V's goal of cheap
    load-time JIT?
  - One translation you specifically chose for correctness over
    speed.
  - The bug that took you longest to find. Describe its symptom and
    its root cause.

## Rubric

| Criterion | Points |
| --------- | ------ |
| Emitter unit tests pass | 15 |
| Slot allocator handles all required registers | 10 |
| Translator covers required RV32I subset | 30 |
| Branch fixups correct | 10 |
| Helper dispatch trampoline correct | 10 |
| All `tests/good/` produce correct results | 15 |
| Writeup quality and AI attribution | 10 |
| **Total** | **100** |

## Common pitfalls

- Forgetting that x86 conditional jumps use *signed* `rel32`. Off-by-
  one math here is endless misery.
- Sign-extension on `lw` (load word). RV `lw` sign-extends the
  32-bit value into the 64-bit register; on x86 use `movsxd`, not
  plain `mov`.
- Not preserving x86 callee-saved registers (`rbx`, `rbp`, `r12–r15`)
  across the program. The C runtime you return into will be surprised.

## What's next

Lab 08 takes the same machinery in-kernel: an out-of-tree module that
loads BPF-V programs via `ioctl`, runs them in kernel mode, and shows
you the seams between user and kernel JIT environments.
