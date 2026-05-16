# 05 — Reference Platforms

Status: starter
Owner: PacketFive
Last reviewed: initial draft

This document lists the hardware and OS targets BPF-V *must* run on to
support the project's claim that the same bytecode runs unchanged
across host CPUs, microcontrollers, FPGA soft cores, and SmartNIC /
accelerator cores. Each platform pins a specific BPF-V profile and a
specific OS configuration.

## 1. Microchip PolarFire SoC — Icicle Kit (rv64gc, 4× U54 + 1× E51)

- SoC: MPFS250T-FCVG484EES (and similar). Quad SiFive U54 (RV64GC) +
  monitor E51 (RV64IMAC).
- BPF-V profile (host Linux): `bpfv-rv64imac-lp64`
  - We deliberately disable F/D for in-kernel programs (G/Fd are
    available to user space).
- BPF-V profile (FPGA fabric soft core, see §1.3): per soft-core
  ISA, typically `bpfv-rv32imc-ilp32` or `bpfv-rv64imc-lp64`.
- OS: Linux (current upstream rv64), Yocto-based mpfs-dev BSP, or
  Zephyr.

### 1.1 Bring-up checklist (host CPU)

- [ ] Boot upstream Linux on Icicle with `CONFIG_BPFV=y`.
- [ ] Run BPF-V selftests (`tools/testing/selftests/bpfv/`).
- [ ] XDP-V "drop" on a USB Ethernet adapter (initial NIC offload is
      software-only on this board).
- [ ] Throughput + verifier-time baselines.

### 1.2 Bring-up checklist (Zephyr on E51)

- [ ] Zephyr build for `mpfs_icicle_kit` E51 monitor core.
- [ ] Minimal BPF-V runtime (`runtime/zephyr/`): loader, verifier
      subset, helper table.
- [ ] Sample: tracing helper that logs an event from an interrupt
      handler.

### 1.3 FPGA soft-core option (PolarFire fabric)

The Icicle's FPGA fabric supports adding RISC-V soft cores
(e.g. Microchip's MiV, or open cores like VexRiscv, CV32E40P). The
plan:

- [ ] Instantiate a `rv32imc` soft core in PolarFire fabric.
- [ ] Connect AXI/AHB bridges to an Ethernet MAC IP.
- [ ] Host CPU sends a verified BPF-V image over AXI to the soft core.
- [ ] Soft core executes BPF-V natively as part of the packet pipeline.

This is the simplest demonstration of the "1:1 RISC-V offload" claim.

## 2. ESP32-C3-DevKitM-1 (rv32imc, Espressif)

- SoC: ESP32-C3 — RV32IMC, no FPU, no MMU.
- BPF-V profile: `bpfv-rv32imc-ilp32`.
- OS: Zephyr or ESP-IDF / FreeRTOS.

### 2.1 Bring-up checklist

- [ ] Zephyr build for `esp32c3_devkitm`.
- [ ] BPF-V runtime trimmed to fit (≈ 20–40 KB target).
- [ ] Sample: BLE/Wi-Fi packet classifier program executing on the C3.
- [ ] Demonstrate cross-compilation: program built on x86 dev machine
      with `riscv32-zephyr-elf-gcc`, image loaded over UART, run on C3.

### 2.2 What this proves

The Icicle is "RISC-V Linux"; the C3 is "RISC-V everything else." If
the same `.bpfv.o` produced by the GCC RISC-V toolchain runs on both
(with profile differences encoded in the meta section), the cross-OS
reusability claim is demonstrated end-to-end.

## 3. Linux on x86\_64 host (cross-target)

- Used as the canonical *non-RISC-V* host, to exercise the real Host
  JIT path (RISC-V → x86\_64 translation).
- Required for CI: developers without an Icicle Kit can still build
  and run the full stack.

## 4. SmartNIC / accelerator class (future)

Tracked here to keep the design grounded; not in Phase 1 deliverables.

- A NIC or DPU with a RISC-V control core (multiple are in development;
  several proprietary, e.g. several CXL accelerators publish RISC-V
  management cores). BPF-V offload would push a verified image to the
  NIC core via PCIe, with the NIC firmware acting as the BPF-V monitor.
- UALink and CXL targets follow the same model: program is the wire
  format, monitor enforces the sandbox.

## 5. Zephyr RTOS as a tier-1 OS target

- Maintain a `runtime/zephyr/` BPF-V runtime that:
  - Validates a small subset of the verifier (the parts feasible in a
    constrained RTOS).
  - Provides a helper table appropriate for RTOS workloads (timers,
    Zephyr logging, sensor APIs).
  - Loads BPF-V images at runtime via a Zephyr shell command or over a
    transport (UART, BLE).
- Ship as an out-of-tree Zephyr module first; upstream once stable.

## 6. CI matrix (target state)

| Builder | Toolchain | Profile | Smoke test |
| ------- | --------- | ------- | ---------- |
| x86\_64 Linux | GCC | host-jit-x86\_64 | selftests, host JIT |
| qemu-system-riscv64 | GCC | rv64imac-lp64 | selftests, pass-through |
| qemu-system-riscv32 | GCC | rv32imc-ilp32 | selftests, pass-through |
| Icicle Kit Linux | GCC | rv64imac-lp64 | XDP-V drop |
| Icicle Kit Zephyr (E51) | GCC | rv64imc-lp64 | trace helper |
| esp32c3\_devkitm Zephyr | GCC | rv32imc-ilp32 | packet classifier |
| x86\_64 Linux | Clang | host-jit-x86\_64 | selftests (Clang-built objects) |

`qemu` boards stand in for "did our compiler produce something the
verifier accepts" and "did the pass-through JIT run it." The physical
boards prove the offload story.
