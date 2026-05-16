# 05 — Reference Platforms

Status: starter
Owner: PacketFive
Last reviewed: initial draft

This document lists the hardware and OS targets MERLIN-V *must* run on to
support the project's claim that the same bytecode runs unchanged
across host CPUs, microcontrollers, FPGA soft cores, and SmartNIC /
accelerator cores. Each platform pins a specific MERLIN-V profile and a
specific OS configuration.

## 1. Microchip PolarFire SoC — Icicle Kit (rv64gc, 4× U54 + 1× E51)

- SoC: MPFS250T-FCVG484EES (and similar). Quad SiFive U54 (RV64GC) +
  monitor E51 (RV64IMAC).
- MERLIN-V profile (host Linux): `merlin-linux-rv64`
  - We deliberately disable F/D for in-kernel programs (G/Fd are
    available to user space).
- MERLIN-V profile (FPGA fabric soft core, see §1.3): `merlin-rtos-rv32`
  (the FPGA soft cores we target are rv32imc-class; if a larger
  soft core is instantiated, switch to `merlin-linux-rv64` and rebuild).
- OS: Linux (current upstream rv64), Yocto-based mpfs-dev BSP, or
  Zephyr.

### 1.1 Bring-up checklist (host CPU)

- [ ] Boot upstream Linux on Icicle with `CONFIG_MERLIN=y`.
- [ ] Run MERLIN-V selftests (`tools/testing/selftests/merlin/`).
- [ ] XDP-V "drop" on a USB Ethernet adapter (initial NIC offload is
      software-only on this board).
- [ ] Throughput + verifier-time baselines.

### 1.2 Bring-up checklist (Zephyr on E51)

- [ ] Zephyr build for `mpfs_icicle_kit` E51 monitor core.
- [ ] Minimal MERLIN-V runtime (`runtime/zephyr/`): loader, verifier
      subset, helper table.
- [ ] Sample: tracing helper that logs an event from an interrupt
      handler.

### 1.3 FPGA soft-core option (PolarFire fabric)

The Icicle's FPGA fabric supports adding RISC-V soft cores
(e.g. Microchip's MiV, or open cores like VexRiscv, CV32E40P). The
plan:

- [ ] Instantiate a `rv32imc` soft core in PolarFire fabric.
- [ ] Connect AXI/AHB bridges to an Ethernet MAC IP.
- [ ] Host CPU sends a verified MERLIN-V image over AXI to the soft core.
- [ ] Soft core executes MERLIN-V natively as part of the packet pipeline.

This is the simplest demonstration of the "1:1 RISC-V offload" claim.

## 2. ESP32-C3-DevKitM-1 (rv32imc, Espressif)

- SoC: ESP32-C3 — RV32IMC, no FPU, no MMU.
- MERLIN-V profile: `merlin-rtos-rv32`.
- OS: Zephyr or ESP-IDF / FreeRTOS.

### 2.1 Bring-up checklist

- [ ] Zephyr build for `esp32c3_devkitm`.
- [ ] MERLIN-V runtime trimmed to fit (≈ 20–40 KB target).
- [ ] Sample: BLE/Wi-Fi packet classifier program executing on the C3.
- [ ] Demonstrate cross-compilation: program built on x86 dev machine
      with `riscv32-zephyr-elf-gcc`, image loaded over UART, run on C3.

### 2.2 What this proves

The Icicle is "RISC-V Linux"; the C3 is "RISC-V everything else." If
the same `.merlin.o` produced by the GCC RISC-V toolchain runs on both
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
  Debian/Ubuntu, so the MERLIN-V kernel module from
  [`03-kernel-interfaces.md`](03-kernel-interfaces.md) and the
  user-space libmerlin build with no special toolchain.
- Demonstrates that MERLIN-V is not an x86-only story on commodity
  Linux: the same `.merlin.o` produced by stock RISC-V GCC runs here,
  with the host JIT translating to AArch64. Coverage parity with
  what eBPF offers on the same board.
- Cheap enough to be in every contributor's hands.

### 4.1 Bring-up checklist

- [ ] Boot upstream Linux on RPi 4B (Raspberry Pi OS / Ubuntu Server
      LTS / Debian arm64).
- [ ] Build the MERLIN-V kernel module from `kernel/merlin/` against the
      running kernel headers.
- [ ] Build and run user-space selftests with the arm64 host JIT.
- [ ] Cross-check: the same `.merlin.o` that runs on x86\_64 Linux
      (host JIT RV→x86\_64) runs here (host JIT RV→arm64) and on the
      MPFS Icicle Kit (pass-through). Identical observable results.

## 5. SmartNIC / accelerator class (future)

Tracked here to keep the design grounded; not in Phase 1 deliverables
and not represented in the current lab inventory.

- A NIC or DPU with a RISC-V control core. MERLIN-V offload would push
  a verified image to the NIC core via PCIe, with the NIC firmware
  acting as the MERLIN-V monitor.
- UALink and CXL targets follow the same model: program is the wire
  format, monitor enforces the sandbox.

When concrete hardware enters the project's lab inventory, this
section will name it; until then no specific device is committed to.

## 6. Zephyr RTOS as a tier-1 OS target

Zephyr is a tier-1 host for MERLIN-V — not an afterthought to the Linux
path. The Zephyr environment is in some ways a *better* match for
MERLIN-V's design than the Linux kernel is, because the things MERLIN-V
provides (sandboxed safety, hot-reloadable logic, a portable
application bytecode) are things Zephyr lacks today.

### 6.1 The gap MERLIN-V fills on Zephyr

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

MERLIN-V's `rtos-rv32/zephyr` (verifier profile) on `merlin-rtos-rv32` profile (see
[`06-verifier.md`](06-verifier.md) §7.2) is purpose-built for this
gap.

### 6.2 Concrete advantages of running MERLIN-V on Zephyr

**1. Safety without a process abstraction.** Zephyr typically has no
MMU — just an MPU — and no process model. Application bugs corrupt
the whole device. MERLIN-V's verifier statically proves memory safety,
termination, well-formed indirect control flow, and helper-only side
effects *before* loading. The device gets memory-safety guarantees
that Zephyr alone does not provide, without paying for an MMU or a
process scheduler.

**2. Field-updatable logic without reflashing the OS.** The Zephyr
image (drivers, BLE stack, MERLIN-V runtime, security boundary) is
long-lived and rarely updated. MERLIN-V modules — sensor pipelines,
classification rules, alarm policy, payload formats — ship and
update separately, over BLE/UART/MQTT/USB, without reboot or full
reflash. Certification authorities can sign just the policy module.

```
+----------------------+
|  Zephyr firmware     |   ← long-lived, OTA-updated rarely
|   - drivers          |
|   - BLE / Wi-Fi stack|
|   - MERLIN-V runtime    |
+----------------------+
        │ loads
        ▼
+----------------------+
|  app.merlin (≤ 64 KB)  |   ← updated freely, no reboot
|   - sensor pipeline  |
|   - classification   |
|   - alarm policy     |
+----------------------+
```

**3. One toolchain, one wire format, one engineer.** Today, a team
shipping firmware uses the Zephyr SDK and a team writing Linux
observability uses libbpf / LLVM-BPF. With MERLIN-V, *the same*
`.merlin.o` runs on both Linux and Zephyr from stock RISC-V GCC or
Clang. Embedded engineers' MERLIN-V expertise becomes portable to
datacentre work and vice versa; hiring and skills transfer improve.

**4. Verifier-enforced real-time bounds.** A MERLIN-V program with the
embedded profile's tight step cap has a *provable* upper bound on
execution time (verifier proves termination; instruction count is
bounded; helper set is restricted to helpers with documented
worst-case latency). That makes MERLIN-V programs schedulable in hard-
real-time contexts in a way arbitrary C application code is not.

**5. Tiny runtime footprint.** The `rtos-rv32/zephyr` (verifier profile) on `merlin-rtos-rv32` profile caps text
at 64 KB and stack at 4 KB. The runtime itself (verifier + loader +
dispatch) targets ~20–40 KB. That fits comfortably on virtually
every modern RISC-V MCU including the ESP32-C3 with room to spare —
typically less than a Wasm runtime in similar regime.

**6. Sandboxed third-party code.** A third-party or partner
integrator ships a signed `.merlin.o`. The device verifies it on
load. Hosting *untrusted modules on your own hardware* becomes a
structural property of the system rather than an act of trust in the
contributor's discipline. This unlocks third-party app stores,
integrator-supplied logic, and partner extensions in models that
plain C cannot safely support.

**7. Hot reload without reboot.** Loading and unloading a MERLIN-V
program is a runtime operation (allocate, verify, relocate, install,
atomic pointer swap). Behaviour can be swapped *without rebooting*
the device — for long-running sensors, actuators, gateways this is
the difference between "a software update" and "an outage."

**8. Cross-board portability of application logic.** Because the
bytecode is RV32IMC (or RV64IMAC) — not a specific MCU's quirks —
the same `app.merlin` runs on the ESP32-C3, the MPFS Icicle E51, and
on RISC-V soft cores synthesised into the Icicle's PolarFire fabric.
**Application logic is decoupled from silicon**, in a way Zephyr
application binaries today are not. As more RISC-V MCUs reach the
project's lab in the future, the same image will run on them
without recompilation.

**9. Auditable, deterministic behaviour.** Verifier-provided
termination and memory-safety proofs make a MERLIN-V module
substantially more auditable than arbitrary C. For safety-critical
contexts (medical, automotive, industrial) this is a real
certification advantage — entire classes of failure mode can be
ruled out structurally rather than by review.

**10. Common observability story across Linux and devices.** BPF /
eBPF gives Linux operators a uniform way to inspect production
behaviour; that story stops at the edge today. With MERLIN-V on Zephyr,
the same toolchain ships tracing and profiling *modules* to the
device, runs them in place, and returns results — the eBPF
observability model on a 100 KB MCU. For fleet operators this is
operationally significant.

### 6.3 The strategic framing

The most interesting framing of MERLIN-V on Zephyr is not "Zephyr gets
a sandbox":

> **MERLIN-V makes the Linux / datacentre eBPF programming model
> available at the deep edge, with the same toolchain, the same wire
> format, the same verifier guarantees, and the same operational
> metaphors.**

A team running an eBPF / Cilium Linux fleet and a Zephyr MCU fleet
today operates two completely separate stacks. With MERLIN-V, *the
application layer* is one stack across both. This is the structural
unlock and most of why MERLIN-V on Zephyr is architecturally more
interesting than MERLIN-V on x86 Linux, where it competes with mature
eBPF.

### 6.4 Where the advantage is weakest (honest bookend)

- For a single-tenant device whose firmware is flashed once and
  never customised, MERLIN-V is overhead. Plain C is fine.
- For devices already shipping a Wasm runtime with plenty of flash,
  the Wasm story is mature; MERLIN-V's win is incremental, not
  transformative.
- For non-RISC-V cores (ARM Cortex-M dominates Zephyr deployments
  today), MERLIN-V still needs a real translating JIT on the device —
  a modest size cost plus a per-arch JIT to maintain. The strongest
  Zephyr-on-MERLIN-V story is on RISC-V MCUs, where the pass-through
  path applies.

### 6.5 Engineering plan (unchanged from prior text)

- Maintain a `runtime/zephyr/` MERLIN-V runtime that:
  - Validates and verifies under the embedded profile's step cap
    (the parts of the verifier feasible in a constrained RTOS).
  - Provides a helper table appropriate for RTOS workloads (timers,
    Zephyr logging, GPIO, sensor APIs, BLE/Wi-Fi gating where the
    product calls for it).
  - Loads MERLIN-V images at runtime via a Zephyr shell command or
    over a transport (UART, BLE, USB-CDC).
- Ship as an out-of-tree Zephyr module first; upstream once stable.

## 7. CI matrix (target state)

The CI matrix is constrained to platforms the project actually has
in its lab inventory: x86\_64 Linux developer hosts, the Raspberry
Pi 4B (arm64 Linux), the MPFS Icicle Kit (RV64 Linux + Zephyr on
E51 / fabric soft cores), and the ESP32-C3-DevKitM-1 (RV32 Zephyr).
QEMU stands in for hermetic, hardware-independent reproduction of
each path.

| Builder | Toolchain | Bytecode profile | Smoke test |
| ------- | --------- | ---------------- | ---------- |
| x86\_64 Linux developer host | GCC | `merlin-linux-rv64` (host JIT to x86\_64) | selftests, host JIT, kernel module |
| x86\_64 Linux developer host | Clang | `merlin-linux-rv64` (host JIT to x86\_64) | selftests (Clang-built objects) |
| Raspberry Pi 4B (arm64 Linux) | GCC | `merlin-linux-rv64` (host JIT to arm64) | selftests, host JIT, kernel module |
| `qemu-system-riscv64` (Linux/virt) | GCC | `merlin-linux-rv64` (pass-through) | selftests, pass-through |
| `qemu-system-riscv32` (Linux/virt) | GCC | `merlin-rtos-rv32` (pass-through) | selftests, pass-through |
| `qemu_riscv64` (Zephyr) | GCC | `merlin-linux-rv64` (pass-through) | trace + GPIO helpers |
| `qemu_riscv32` (Zephyr) | GCC | `merlin-rtos-rv32` (pass-through) | trace + GPIO helpers |
| MPFS Icicle Kit (Linux on U54) | GCC | `merlin-linux-rv64` (pass-through) | XDP-V drop |
| MPFS Icicle Kit (Zephyr on E51) | GCC | `merlin-rtos-rv32` (pass-through) | trace helper |
| MPFS Icicle fabric soft core (rv32imc) | GCC | `merlin-rtos-rv32` (pass-through) | offload demonstrator |
| `esp32c3_devkitm` (Zephyr) | GCC | `merlin-rtos-rv32` (pass-through) | packet classifier |

`qemu` boards stand in for "did our compiler produce something the
verifier accepts" and "did the pass-through JIT run it." The physical
boards prove the offload story.
