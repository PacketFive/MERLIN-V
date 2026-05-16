# MERLIN-V — Advanced Operating Systems Design Course

> **Audience.** Graduate-level / senior-undergraduate students who have
> completed an introductory operating systems course (process model,
> virtual memory, system calls, scheduling) and a systems-programming
> course in C. Familiarity with the Linux kernel build is helpful but
> not required.
>
> **Anchor project.** MERLIN-V — a clean-room, in-kernel JIT VM whose
> bytecode is the RISC-V ISA. See [`../design/`](../design/) for the
> design record this course is built around.

## 1. Course goals

By the end of the course, a student can:

- Explain how eBPF works end-to-end: loader, verifier, JIT, maps,
  helpers, hooks, BTF, CO-RE.
- Read and write small RISC-V assembly and disassemble compiler output.
- Implement, in user space, a minimal verifier (abstract interpreter)
  and a minimal JIT for a restricted RISC-V profile.
- Build, load, and run an out-of-tree Linux kernel module that hosts a
  toy "MERLIN-V" runtime, and understand the interactions with kernel
  memory, IPIs, and I-cache coherence.
- Port the same runtime to Zephyr RTOS on a RISC-V microcontroller and
  explain the differences from the Linux path.
- Reason about the verifier as a soundness boundary and discuss the
  consequences of a verifier bug in a production system.
- Critique the MERLIN-V design honestly: what it improves over eBPF,
  what it makes harder, and where the open questions are.

## 2. Prerequisites

Required:
- C programming at the level of K&R chapters 1–8, plus comfort with
  `Makefile` and pointer-heavy code.
- An OS course covering process address spaces, virtual memory, page
  tables, and system calls.
- Comfort on a Linux shell.

Helpful:
- Prior exposure to assembly (any ISA).
- Prior exposure to compiler internals (parsing, IR, codegen).
- A reading of *Operating Systems: Three Easy Pieces* chapters on
  virtualization and persistence, or equivalent.

Strict non-prerequisites (we teach them in-course):
- Knowledge of RISC-V specifically.
- Knowledge of eBPF specifically.
- Kernel module development.

## 3. Lab map

| # | Lab | Theme | Effort tier |
| - | --- | ----- | ----------- |
| [00](lab-00-environment-setup.md) | Environment setup | Toolchains, QEMU, kernel build, Zephyr SDK, submodules | S |
| [01](lab-01-ebpf-dissection.md) | eBPF dissection | Build, load, dump, and trace a real eBPF program; learn the moving parts | M |
| [02](lab-02-riscv-isa-primer.md) | RISC-V ISA primer | Read RV32IM / RV64IM, disassemble GCC and Clang output, identify ABI conventions | M |
| [03](lab-03-userland-merlin-interp.md) | User-space MERLIN-V interpreter | Decode and execute a small RV32I program from an ELF in user space | M |
| [04](lab-04-minimal-verifier.md) | Minimal verifier | Abstract interpretation: register types, ranges, pointer provenance | L |
| [05](lab-05-merlin-objtool.md) | `merlin-objtool` | ELF parsing, `.merlin.*` section validation, relocation cataloguing | M |
| [06](lab-06-passthrough-jit.md) | Pass-through JIT (user space) | `mmap` RX, copy text, fix relocs, `__builtin___clear_cache`, jump | M |
| [07](lab-07-host-jit-x86_64.md) | Host JIT for x86\_64 | Single-pass RISC-V → x86\_64 translator with a tiny peephole pass | L |
| [08](lab-08-kernel-module.md) | Out-of-tree kernel module | Char device + `ioctl` + executable page allocation + I-cache flush | L |
| [09](lab-09-zephyr-runtime.md) | Zephyr runtime | Port the user-space runtime to Zephyr; run on `qemu_riscv32` | M |
| [10](lab-10-hardware-bringup.md) | Hardware bring-up | Run the Zephyr runtime on ESP32-C3 or MPFS Icicle Kit | L |
| [11](lab-11-capstone.md) | Capstone: XDP-V drop with measurements | End-to-end demonstrator + writeup | L |

Effort tiers:
- **S** — Small. Tooling and reading; mostly mechanical.
- **M** — Medium. A focused implementation task with a clear spec.
- **L** — Large. Multi-component; expect to debug across the stack.

## 4. Assessment

| Component | Weight |
| --------- | ------ |
| Lab submissions (00–11), automatic grader + code review | 60% |
| Midterm: written design exam covering Labs 00–06 | 15% |
| Capstone writeup (Lab 11) | 20% |
| Class participation / paper discussions | 5% |

Each lab carries:
- A **submission bundle** (code, build artefacts, a short writeup).
- An **automatic grader** ("autograder") that runs the test harness
  shipped with the lab.
- A **rubric** documented at the bottom of the lab's README.

Late and resubmission policy lives in the offering instance's syllabus,
not in this repository.

## 5. Reading list

Distributed across labs; consolidated here.

### 5.1 Primary

- Linux kernel BPF documentation:
  <https://docs.kernel.org/bpf/index.html>
- *The Linux Observability with BPF* (Calavera & Fontana). Chapters
  on the BPF VM, verifier, and helpers.
- *BPF Performance Tools* (Gregg). Background on how eBPF is used in
  practice.
- The [RISC-V Instruction Set Manual](https://riscv.org/specifications/),
  Vol I (User-Level) and Vol II (Privileged).
- The [RISC-V ABIs Specification](https://github.com/riscv-non-isa/riscv-elf-psabi-doc).

### 5.2 Secondary

- Bonzini, "[Performance analysis of BPF JIT compilers](https://dl.acm.org/doi/10.1145/3458336.3465288)"
  and follow-ups.
- Gershuni et al., "[Simple and Precise Static Analysis of Untrusted Linux Kernel Extensions](https://dl.acm.org/doi/10.1145/3314221.3314590)"
  (PLDI '19) — the eBPF verifier from an abstract-interpretation lens.
- Vahldiek-Oberwagner et al., "[ERIM: Secure, Efficient In-process Isolation](https://www.usenix.org/conference/usenixsecurity19/presentation/vahldiek-oberwagner)"
  (USENIX Security '19) — background on in-process sandboxing.
- *RISC-V Reader* (Patterson & Waterman) — short, friendly ISA tour.

### 5.3 In-tree references

- `bpf-next/Documentation/bpf/` (submodule of this repo).
- `bpf-next/kernel/bpf/`, especially `verifier.c`, `core.c`,
  `syscall.c`.
- `bpf-next/arch/riscv/net/bpf_jit_*.c` — eBPF JIT *targeting* RISC-V,
  useful as a study of single-pass JIT design.

## 6. Reproducibility and grading infrastructure

Every lab MUST be reproducible on:

- An x86\_64 Linux developer workstation (Ubuntu LTS or Fedora current).
- A QEMU-emulated RISC-V target.

Each lab ships:

- A `Makefile` invoking only ecosystem tools.
- A `tests/` directory with golden inputs/outputs and a small test
  driver.
- A `solutions/` directory **kept in a separate, instructor-only
  branch**, never on `main`.
- A `tools/grade.sh` script that the autograder invokes.

Hardware-only labs (10, optionally 11) have a software-only fallback
path graded on QEMU.

## 7. AI policy

This course uses MERLIN-V's [AI agent policy](../AI/AGENT_INSTRUCTIONS.md)
as its baseline for AI assistance:

- AI-generated code is **permitted** but must be:
  - Read and understood by the submitter line-by-line.
  - Attributed in the submission writeup using the project's
    `Assisted-by:` format (model family name only, no version — see
    [`../AI/ATTRIBUTION.md`](../AI/ATTRIBUTION.md)).
- The midterm and capstone defence are **AI-free**.
- Soliciting an AI to "solve the lab" without engagement is treated as
  academic dishonesty.

Instructors customising this course should adapt this section to their
institution's policies.

## 8. Course-to-design map

If a student wants to see how their lab feeds the real design:

| Lab | Design doc(s) it touches |
| --- | ------------------------ |
| 00 | none directly — setup |
| 01 | [`01-ebpf-comparative-analysis.md`](../design/01-ebpf-comparative-analysis.md) |
| 02 | [`02-isa-and-bytecode.md`](../design/02-isa-and-bytecode.md) |
| 03 | [`02`](../design/02-isa-and-bytecode.md), [`07`](../design/07-jit-and-offload.md) |
| 04 | [`06-verifier.md`](../design/06-verifier.md) |
| 05 | [`02`](../design/02-isa-and-bytecode.md), [`04-toolchain.md`](../design/04-toolchain.md) |
| 06 | [`07-jit-and-offload.md`](../design/07-jit-and-offload.md) §2 |
| 07 | [`07-jit-and-offload.md`](../design/07-jit-and-offload.md) §3 |
| 08 | [`03-kernel-interfaces.md`](../design/03-kernel-interfaces.md) |
| 09 | [`05-reference-platforms.md`](../design/05-reference-platforms.md) §5 |
| 10 | [`05-reference-platforms.md`](../design/05-reference-platforms.md) §1–§2 |
| 11 | all of the above |

## 9. Acknowledgements

This course is built on the public work of the Linux kernel BPF
subsystem maintainers and the RISC-V community. See [`../design/`](../design/)
for the project's design record and the
[AI policy](../AI/AGENT_INSTRUCTIONS.md) for contribution rules.
