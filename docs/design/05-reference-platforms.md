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

- Used as the canonical *non-RISC-V x86* host, to exercise the real
  Host JIT path (RISC-V → x86\_64 translation).
- Required for CI: developers without an Icicle Kit can still build
  and run the full stack.

## 4. Raspberry Pi 4B (arm64 Linux, cross-target)

- Used as the canonical *non-RISC-V arm64* host, to exercise the
  real Host JIT path (RISC-V → arm64 translation).
- Cortex-A72 quad-core, 64-bit. Runs upstream Linux + standard
  Debian/Ubuntu, so the BPF-V kernel module from
  [`03-kernel-interfaces.md`](03-kernel-interfaces.md) and the
  user-space libbpfv build with no special toolchain.
- Demonstrates that BPF-V is not an x86-only story on commodity
  Linux: the same `.bpfv.o` produced by stock RISC-V GCC runs here,
  with the host JIT translating to AArch64. Coverage parity with
  what eBPF offers on the same board.
- Cheap enough to be in every contributor's hands.

### 4.1 Bring-up checklist

- [ ] Boot upstream Linux on RPi 4B (Raspberry Pi OS / Ubuntu Server
      LTS / Debian arm64).
- [ ] Build the BPF-V kernel module from `kernel/bpfv/` against the
      running kernel headers.
- [ ] Build and run user-space selftests with the arm64 host JIT.
- [ ] Cross-check: the same `.bpfv.o` that runs on x86\_64 Linux
      (host JIT RV→x86\_64) runs here (host JIT RV→arm64) and on the
      MPFS Icicle Kit (pass-through). Identical observable results.

## 5. SmartNIC / accelerator class (future)

Tracked here to keep the design grounded; not in Phase 1 deliverables
and not represented in the current lab inventory.

- A NIC or DPU with a RISC-V control core. BPF-V offload would push
  a verified image to the NIC core via PCIe, with the NIC firmware
  acting as the BPF-V monitor.
- UALink and CXL targets follow the same model: program is the wire
  format, monitor enforces the sandbox.

When concrete hardware enters the project's lab inventory, this
section will name it; until then no specific device is committed to.

## 6. Zephyr RTOS as a tier-1 OS target

Zephyr is a tier-1 host for BPF-V — not an afterthought to the Linux
path. The Zephyr environment is in some ways a *better* match for
BPF-V's design than the Linux kernel is, because the things BPF-V
provides (sandboxed safety, hot-reloadable logic, a portable
application bytecode) are things Zephyr lacks today.

### 6.1 The gap BPF-V fills on Zephyr

Zephyr's current deployment model freezes application behaviour at
flash time. A/B image swap with full reflash is the canonical update
mechanism. Other ecosystems plug this gap with:

- **MicroPython / Lua on MCUs** — flexible but unsafe (a script can
  crash the device or wedge the scheduler), slow (interpreted), and
  large (hundreds of KB).
- **WebAssembly micro-runtimes (WAMR, wasm3)** — safe and reasonably
  fast, but require a Wasm-aware toolchain and 50–200 KB of runtime
  unrelated to the device's actual job.
- **Custom plugin loaders** — common in practice, almost always
  unsafe and bespoke.

BPF-V's `rtos/zephyr` profile (see
[`06-verifier.md`](06-verifier.md) §7.2) is purpose-built for this
gap.

### 6.2 Concrete advantages of running BPF-V on Zephyr

**1. Safety without a process abstraction.** Zephyr typically has no
MMU — just an MPU — and no process model. Application bugs corrupt
the whole device. BPF-V's verifier statically proves memory safety,
termination, well-formed indirect control flow, and helper-only side
effects *before* loading. The device gets memory-safety guarantees
that Zephyr alone does not provide, without paying for an MMU or a
process scheduler.

**2. Field-updatable logic without reflashing the OS.** The Zephyr
image (drivers, BLE stack, BPF-V runtime, security boundary) is
long-lived and rarely updated. BPF-V modules — sensor pipelines,
classification rules, alarm policy, payload formats — ship and
update separately, over BLE/UART/MQTT/USB, without reboot or full
reflash. Certification authorities can sign just the policy module.

```
+----------------------+
|  Zephyr firmware     |   ← long-lived, OTA-updated rarely
|   - drivers          |
|   - BLE / Wi-Fi stack|
|   - BPF-V runtime    |
+----------------------+
        │ loads
        ▼
+----------------------+
|  app.bpfv (≤ 64 KB)  |   ← updated freely, no reboot
|   - sensor pipeline  |
|   - classification   |
|   - alarm policy     |
+----------------------+
```

**3. One toolchain, one wire format, one engineer.** Today, a team
shipping firmware uses the Zephyr SDK and a team writing Linux
observability uses libbpf / LLVM-BPF. With BPF-V, *the same*
`.bpfv.o` runs on both Linux and Zephyr from stock RISC-V GCC or
Clang. Embedded engineers' BPF-V expertise becomes portable to
datacentre work and vice versa; hiring and skills transfer improve.

**4. Verifier-enforced real-time bounds.** A BPF-V program with the
embedded profile's tight step cap has a *provable* upper bound on
execution time (verifier proves termination; instruction count is
bounded; helper set is restricted to helpers with documented
worst-case latency). That makes BPF-V programs schedulable in hard-
real-time contexts in a way arbitrary C application code is not.

**5. Tiny runtime footprint.** The `rtos/zephyr` profile caps text
at 64 KB and stack at 4 KB. The runtime itself (verifier + loader +
dispatch) targets ~20–40 KB. That fits comfortably on virtually
every modern RISC-V MCU including the ESP32-C3 with room to spare —
typically less than a Wasm runtime in similar regime.

**6. Sandboxed third-party code.** A third-party or partner
integrator ships a signed `.bpfv.o`. The device verifies it on
load. Hosting *untrusted modules on your own hardware* becomes a
structural property of the system rather than an act of trust in the
contributor's discipline. This unlocks third-party app stores,
integrator-supplied logic, and partner extensions in models that
plain C cannot safely support.

**7. Hot reload without reboot.** Loading and unloading a BPF-V
program is a runtime operation (allocate, verify, relocate, install,
atomic pointer swap). Behaviour can be swapped *without rebooting*
the device — for long-running sensors, actuators, gateways this is
the difference between "a software update" and "an outage."

**8. Cross-board portability of application logic.** Because the
bytecode is RV32IMC (or RV64IMAC) — not a specific MCU's quirks —
the same `app.bpfv` runs on the ESP32-C3, the MPFS Icicle E51, and
on RISC-V soft cores synthesised into the Icicle's PolarFire fabric.
**Application logic is decoupled from silicon**, in a way Zephyr
application binaries today are not. As more RISC-V MCUs reach the
project's lab in the future, the same image will run on them
without recompilation.

**9. Auditable, deterministic behaviour.** Verifier-provided
termination and memory-safety proofs make a BPF-V module
substantially more auditable than arbitrary C. For safety-critical
contexts (medical, automotive, industrial) this is a real
certification advantage — entire classes of failure mode can be
ruled out structurally rather than by review.

**10. Common observability story across Linux and devices.** BPF /
eBPF gives Linux operators a uniform way to inspect production
behaviour; that story stops at the edge today. With BPF-V on Zephyr,
the same toolchain ships tracing and profiling *modules* to the
device, runs them in place, and returns results — the eBPF
observability model on a 100 KB MCU. For fleet operators this is
operationally significant.

### 6.3 The strategic framing

The most interesting framing of BPF-V on Zephyr is not "Zephyr gets
a sandbox":

> **BPF-V makes the Linux / datacentre eBPF programming model
> available at the deep edge, with the same toolchain, the same wire
> format, the same verifier guarantees, and the same operational
> metaphors.**

A team running an eBPF / Cilium Linux fleet and a Zephyr MCU fleet
today operates two completely separate stacks. With BPF-V, *the
application layer* is one stack across both. This is the structural
unlock and most of why BPF-V on Zephyr is architecturally more
interesting than BPF-V on x86 Linux, where it competes with mature
eBPF.

### 6.4 Where the advantage is weakest (honest bookend)

- For a single-tenant device whose firmware is flashed once and
  never customised, BPF-V is overhead. Plain C is fine.
- For devices already shipping a Wasm runtime with plenty of flash,
  the Wasm story is mature; BPF-V's win is incremental, not
  transformative.
- For non-RISC-V cores (ARM Cortex-M dominates Zephyr deployments
  today), BPF-V still needs a real translating JIT on the device —
  a modest size cost plus a per-arch JIT to maintain. The strongest
  Zephyr-on-BPF-V story is on RISC-V MCUs, where the pass-through
  path applies.

### 6.5 Engineering plan (unchanged from prior text)

- Maintain a `runtime/zephyr/` BPF-V runtime that:
  - Validates and verifies under the embedded profile's step cap
    (the parts of the verifier feasible in a constrained RTOS).
  - Provides a helper table appropriate for RTOS workloads (timers,
    Zephyr logging, GPIO, sensor APIs, BLE/Wi-Fi gating where the
    product calls for it).
  - Loads BPF-V images at runtime via a Zephyr shell command or
    over a transport (UART, BLE, USB-CDC).
- Ship as an out-of-tree Zephyr module first; upstream once stable.

## 7. CI matrix (target state)

The CI matrix is constrained to platforms the project actually has
in its lab inventory: x86\_64 Linux developer hosts, the Raspberry
Pi 4B (arm64 Linux), the MPFS Icicle Kit (RV64 Linux + Zephyr on
E51 / fabric soft cores), and the ESP32-C3-DevKitM-1 (RV32 Zephyr).
QEMU stands in for hermetic, hardware-independent reproduction of
each path.

| Builder | Toolchain | Profile | Smoke test |
| ------- | --------- | ------- | ---------- |
| x86\_64 Linux developer host | GCC | host-jit-x86\_64 | selftests, host JIT, kernel module |
| x86\_64 Linux developer host | Clang | host-jit-x86\_64 | selftests (Clang-built objects) |
| Raspberry Pi 4B (arm64 Linux) | GCC | host-jit-arm64 | selftests, host JIT, kernel module |
| `qemu-system-riscv64` (Linux/virt) | GCC | rv64imac-lp64 | selftests, pass-through |
| `qemu-system-riscv32` (Linux/virt) | GCC | rv32imc-ilp32 | selftests, pass-through |
| `qemu_riscv64` (Zephyr) | GCC | rv64imac-lp64 | trace + GPIO helpers |
| `qemu_riscv32` (Zephyr) | GCC | rv32imc-ilp32 | trace + GPIO helpers |
| MPFS Icicle Kit (Linux on U54) | GCC | rv64imac-lp64 | XDP-V drop |
| MPFS Icicle Kit (Zephyr on E51) | GCC | rv64imc-lp64 | trace helper |
| MPFS Icicle fabric soft core (rv32imc) | GCC | rv32imc-ilp32 | offload demonstrator |
| `esp32c3_devkitm` (Zephyr) | GCC | rv32imc-ilp32 | packet classifier |

`qemu` boards stand in for "did our compiler produce something the
verifier accepts" and "did the pass-through JIT run it." The physical
boards prove the offload story.
