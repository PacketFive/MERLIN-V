# BPF-V

**BPF-V — a clean-room, in-kernel JIT VM whose bytecode is the RISC-V ISA.**

BPF-V is a from-scratch design of an in-kernel JIT virtual machine
analogous to eBPF but with one decisive change: instead of a custom
64-bit RISC-like bytecode, **BPF-V programs are encoded in a restricted
profile of the RISC-V ISA (RV32 and RV64)**. The same verified image
runs:

- On RISC-V Linux hosts — via a *pass-through JIT* (no instruction
  translation).
- On non-RISC-V hosts (x86\_64, arm64, …) — via a small host JIT that
  translates RISC-V to the host ISA.
- On RISC-V SmartNICs and PCIe / CXL / UALink accelerators with RISC-V
  cores — by direct load, 1:1, no re-translation.
- On microcontrollers (ESP32-C3, MPFS Icicle E51 core) under
  Zephyr RTOS, sharing the same toolchain and object format.

**Maintainer:** PacketFive.
**License:** GPL-2.0-only (in-kernel); dual GPL-2.0 / BSD-2-Clause for
UAPI headers and user-space libraries (`libbpfv`).

## Repository layout

```
.
├── docs/
│   ├── AI/                # AI agent contribution policy (Linux Kernel AI Policy)
│   ├── design/            # BPF-V design documents (RFC + paper source material)
│   └── academics/         # Advanced-OS-design training course (12 labs + syllabus)
├── net-next/              # submodule: cutting-edge Linux networking tree (reference only)
├── bpf-next/              # submodule: BPF subsystem tree (reference only; eBPF/XDP/AF_XDP/…)
├── LICENSE
└── README.md
```

The two kernel submodules are **read-only references**; we track them
to stay current with upstream networking and BPF development. All
BPF-V design and source contributions live under this repository's own
directories.

## Start here

- [`docs/design/README.md`](docs/design/README.md) — index of design
  documents.
- [`docs/design/00-overview.md`](docs/design/00-overview.md) — vision,
  goals/non-goals, glossary.
- [`docs/design/01-ebpf-comparative-analysis.md`](docs/design/01-ebpf-comparative-analysis.md) —
  what eBPF is, and where BPF-V diverges.
- [`docs/academics/README.md`](docs/academics/README.md) — the
  advanced-OS-design course built on top of BPF-V (twelve labs,
  setup → eBPF dissection → user-space stack → kernel module →
  Zephyr / hardware → capstone).
- [`docs/AI/AGENT_INSTRUCTIONS.md`](docs/AI/AGENT_INSTRUCTIONS.md) —
  rules for AI-assisted contributions (follows the Linux Kernel AI
  Policy).

## Status

Pre-RFC. The design documents are *starters* intended to be matured
into an LKML RFC patch series and an accompanying paper.

## References

- Linux kernel BPF documentation:
  <https://docs.kernel.org/bpf/index.html>
- Linux Kernel AI Policy:
  <https://docs.kernel.org/process/coding-assistants.html>
- RISC-V specifications: <https://riscv.org/specifications/>
- Zephyr RTOS: <https://www.zephyrproject.org/>
