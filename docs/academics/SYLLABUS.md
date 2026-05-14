# Syllabus — BPF-V Advanced OS Design Course

> **Pacing.** Organised in *modules*, not weeks. Each module assumes
> the prior module's deliverable is complete. An instructor adapting
> this for a quarter, semester, or block course assigns modules to
> calendar slots in their own syllabus.

## Module map

```
┌──────────────────────┐
│  Module 1 — Setup    │  Lab 00
└─────────┬────────────┘
          ▼
┌──────────────────────┐
│  Module 2 — Context  │  Lab 01 (eBPF) → Lab 02 (RISC-V)
└─────────┬────────────┘
          ▼
┌──────────────────────────┐
│  Module 3 — User-space   │  Lab 03 (interpreter) → Lab 04 (verifier)
│  BPF-V core              │   → Lab 05 (objtool)
└─────────┬────────────────┘
          ▼
┌──────────────────────────┐
│  Module 4 — JITs         │  Lab 06 (pass-through) → Lab 07 (host JIT)
└─────────┬────────────────┘
          ▼
        Midterm (written design exam)
          ▼
┌──────────────────────────┐
│  Module 5 — Kernel       │  Lab 08 (out-of-tree kmod)
└─────────┬────────────────┘
          ▼
┌──────────────────────────┐
│  Module 6 — Embedded     │  Lab 09 (Zephyr/QEMU) → Lab 10 (HW)
└─────────┬────────────────┘
          ▼
┌──────────────────────────┐
│  Module 7 — Capstone     │  Lab 11 (XDP-V drop + measurements)
└──────────────────────────┘
```

## Module learning outcomes

### Module 1 — Setup
- Build a working RISC-V cross-toolchain (GCC and Clang).
- Boot a `qemu-system-riscv64` instance with a minimal Linux rootfs.
- Build the kernel from this repo's `bpf-next` submodule.
- Build a Zephyr "Hello World" for `qemu_riscv32`.
- Initialise both submodules and document submodule policy.

### Module 2 — Context
- Build and run an eBPF program; read its disassembly; observe the
  verifier log.
- Read RISC-V assembly; reproduce small functions in `as`.
- Articulate, in one page, why a BPF-V proposal exists.

### Module 3 — User-space BPF-V core
- Implement a decoder/interpreter for an `RV32I` subset.
- Implement a verifier: register state lattice, range tracking,
  pointer provenance, indirect-jump rejection, loop bound proofs.
- Parse a BPF-V ELF: `.text`, `.bpfv.meta`, `.bpfv.maps`,
  `.bpfv.relocs`.

### Module 4 — JITs
- Implement a pass-through "JIT": `mmap(PROT_READ|PROT_EXEC)`, copy
  the verified `.text`, fix relocations, flush the I-cache, jump.
- Implement a one-pass RISC-V → x86\_64 translator with a small
  peephole; reuse the verifier's output.

### Module 5 — Kernel
- Build and load an out-of-tree kernel module exposing `ioctl(2)` to
  submit a program and execute it.
- Allocate executable kernel pages (`__vmalloc_node_range` with
  `PAGE_KERNEL_EXEC`), flush caches, and run safely under PREEMPT-RT
  rules.
- Demonstrate the security boundary by feeding bad programs and
  watching them get rejected.

### Module 6 — Embedded
- Build the Zephyr runtime; run on `qemu_riscv32` with a UART loader.
- Bring up on ESP32-C3 *or* MPFS Icicle (E51 monitor core); document
  the porting delta.

### Module 7 — Capstone
- Implement a small XDP-V-equivalent hook in the out-of-tree module,
  attach to a `veth` pair, count and drop packets matching a filter.
- Produce a measurement report (verifier time, load-to-run latency,
  per-packet cycles) and a short critique of the BPF-V design.

## Assessment artefacts per lab

Every lab submission contains:

1. **Code** under `src/`.
2. **Tests** under `tests/` (the lab ships a baseline; students may
   add more, but the baseline must pass).
3. **A `WRITEUP.md`** of at most 1500 words covering:
   - Approach.
   - Design alternatives considered.
   - Bugs encountered and fixes.
   - AI assistance, if any, with `Assisted-by:` attribution per project
     policy.
4. **The autograder log** showing a passing run.

## Recommended order of operations within each lab

1. Read the lab's `README.md` end to end before coding.
2. Read the linked design document section(s) before coding.
3. Run the provided baseline tests on the unmodified skeleton; they
   should fail with informative messages.
4. Implement the required interfaces; iterate until tests pass.
5. Write `WRITEUP.md` while the work is fresh.
6. Run `tools/grade.sh` and ensure the autograder log is clean.
7. Submit.

## Instructor notes

- The `solutions/` directory for each lab lives on a separate,
  instructor-only branch (e.g. `course/solutions`). It must not be
  merged to `main`.
- The autograder is intentionally deterministic and seed-pinned for
  every randomised test. Any non-determinism in student code is a bug.
- If a lab depends on hardware the student doesn't have, the software
  fallback path is mandatory to ship. Treat Lab 10 specifically as
  "students with hardware do the full path; students without do the
  QEMU path."
