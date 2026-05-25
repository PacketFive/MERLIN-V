# MERLIN-V Design Documentation

> **Project:** MERLIN-V — a clean-room, in-kernel JIT VM whose bytecode IS
> the RISC-V ISA (RV32 + RV64), designed for 1:1 hardware offload to
> RISC-V CPUs, MCUs, and RISC-V-based PCIe / CXL / UALink accelerators
> and SmartNICs.
>
> **Maintainer:** PacketFive
> **License:** GPL-2.0-only (kernel components), dual GPL-2.0 / BSD-2-Clause
> where appropriate (UAPI headers, user-space libraries) — TBD per file.

This directory holds the *design record* for MERLIN-V. Documents here are
the source material for:

- Conference / journal papers proposing MERLIN-V.
- An RFC patch series to LKML (`bpf`, `netdev`, `linux-riscv`).
- Reference-platform bring-up notes (Microchip PolarFire SoC Icicle Kit,
  ESP32-C3-DevKitM-1, Zephyr RTOS).

## Reading order

| # | Document | Status |
| - | -------- | ------ |
| 00 | [`00-overview.md`](00-overview.md) — vision, goals, non-goals, glossary | starter |
| 01 | [`01-ebpf-comparative-analysis.md`](01-ebpf-comparative-analysis.md) — eBPF anatomy and where MERLIN-V diverges | starter |
| 02 | [`02-isa-and-bytecode.md`](02-isa-and-bytecode.md) — the MERLIN-V ISA profile on top of RV32/RV64 | starter |
| 03 | [`03-kernel-interfaces.md`](03-kernel-interfaces.md) — uapi, syscall surface, loader, maps | draft |
| 04 | [`04-toolchain.md`](04-toolchain.md) — GCC vs LLVM, libmerlin, MERLIN BTF, CO-RE-V | draft |
| 05 | [`05-reference-platforms.md`](05-reference-platforms.md) — Icicle Kit, ESP32-C3, Zephyr | starter |
| 06 | [`06-verifier.md`](06-verifier.md) — verifier strategy on a permissive ISA | starter |
| 07 | [`07-jit-and-offload.md`](07-jit-and-offload.md) — host JIT, NIC/accel offload | draft |
| 08 | [`08-mvdp-and-af-mvdp.md`](08-mvdp-and-af-mvdp.md) — MVDP program type and AF\_MVDP socket family (unified socket model) | draft |
| 09 | [`09-mvcp-kernel-uapi.md`](09-mvcp-kernel-uapi.md) — MVCP layer A: in-kernel control-plane primitives | draft |
| 10 | [`10-mvcp-daemon-and-fleet.md`](10-mvcp-daemon-and-fleet.md) — MVCP layer B: `merlind` reference daemon and fleet semantics | draft |
| 11 | [`11-mvcp-attestation.md`](11-mvcp-attestation.md) — MVCP attestation protocol and HW chain | draft |
| 12 | [`12-core-v-spec.md`](12-core-v-spec.md) — CO-RE-V relocation and `.merlin.btf_ext` spec | draft |
| 13 | [`13-evaluation-plan.md`](13-evaluation-plan.md) — RFC v1 evaluation plan: workloads, environments, pass/fail criteria | starter |
| 14 | [`14-mvcp-multicast.md`](14-mvcp-multicast.md) — MVCP multicast channel: `MERLIN_NL_CTRL` Genl family + event schemas | design |
| 15 | [`15-verifier-phase2.md`](15-verifier-phase2.md) — Phase-2 verifier as landed: CFG + worklist + tnum + bounded loops | landed |

For the course/training-material companion to these design docs, see
[`../academics/`](../academics/README.md).

Draft kernel UAPI headers (the source of truth for layouts and
constants once they exist) live under [`uapi/`](uapi/README.md).

Each document starts as a *starter* (skeleton with the framing committed
in writing), then evolves into a *draft* (complete first pass, internally
consistent), and finally a *stable* design (frozen for RFC / paper
submission). Move documents between states by changing the `Status:`
header at the top.

## Working agreement

- Every design change goes in via a normal patch to `docs/design/`.
- AI-assisted edits follow [`docs/AI/AGENT_INSTRUCTIONS.md`](../AI/AGENT_INSTRUCTIONS.md)
  and [`docs/AI/ATTRIBUTION.md`](../AI/ATTRIBUTION.md). In particular, AI
  agents do not commit and use the project `Assisted-by:` format
  (model family name only, no version).
- Submodules `net-next/` and `bpf-next/` are reference material only.
  We track upstream and cite by path/commit, but we **do not modify**
  them from this repository.
- Kernel coding style and patch discipline (Linux Kernel
  [`process/`](https://docs.kernel.org/process/index.html)) apply to all
  in-tree C contributions.
