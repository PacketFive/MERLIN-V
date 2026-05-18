# platforms/esp32c3/ — MERLIN-V on the ESP32-C3-DevKitM-1

Status: **bring-up scaffolding**
Cross-reference: `docs/design/05-reference-platforms.md §2`

---

## Target board

- **Board**: Espressif ESP32-C3-DevKitM-1
- **SoC**: ESP32-C3 — RV32IMC, single core @ 160 MHz, no FPU, no MMU
- **Flash**: 4 MiB on-module
- **SRAM**: 400 KiB
- **MERLIN-V profile**: `merlin-rtos-rv32` (rv32imc_zicsr_zifencei)
- **Verifier profile**: `rtos-rv32/zephyr`
- **OS**: Zephyr RTOS (this directory); ESP-IDF is parallel work
  (not in this prototype)

---

## What this directory contains

```
platforms/esp32c3/
├── README.md              this file
├── BRINGUP.md             step-by-step bring-up instructions
├── boards/
│   └── esp32c3_devkitm.overlay   Devicetree overlay (UART pinmux)
├── prj.conf               Project Kconfig for the C3 sample
├── helpers/
│   ├── helpers_esp32c3.c  Board-tied helpers (gpio_set/get via DT)
│   └── CMakeLists.txt
└── sample-classifier/
    ├── CMakeLists.txt
    ├── prj.conf
    ├── src/
    │   ├── main.c             demo entry
    │   ├── classifier_blob.c  hand-rolled .merlin.o
    │   └── classifier_src.S   readable source (assemble offline)
    └── sample.yaml
```

`sample-classifier` is the bring-up flagship: a verifier-accepted
MERLIN-V program that walks a fake Ethernet/IP packet header in
SRAM and returns a verdict code. Demonstrates:

- Loading a `.merlin.o` blob at runtime on a 400 KiB SRAM MCU
- Verifier acceptance on the rtos-rv32 profile
- Pass-through execution on a real RV32IMC core
- Hot-swap: replace the classifier image without rebooting

---

## Quick start

```bash
# From the MERLIN-V repo root.  ZEPHYR_BASE set; Zephyr SDK installed
# with the riscv32 toolchain; esp32c3_devkitm board connected at /dev/ttyUSB0.

west build -b esp32c3_devkitm \
    -p always platforms/esp32c3/sample-classifier \
    -- -DEXTRA_ZEPHYR_MODULES="$(pwd)/zephyr/merlin"

west flash
west espressif monitor   # ctrl-] to exit
```

Expected console output:

```
*** Booting Zephyr OS build vX.Y.Z ***
merlin: runtime initialised (slots=4, max_bytes=16384, stack=256)
classifier: boot on esp32c3 (RV32IMC, no MMU)
classifier: loaded id=1 insns=N
classifier: synthetic packet ETH/IPv4/TCP → verdict=2 (PASS)
classifier: synthetic packet ETH/ARP    → verdict=1 (DROP)
classifier: done
```

See `BRINGUP.md` for the step-by-step procedure including
toolchain install, flash-recovery, and serial-monitor caveats.

---

## What this platform proves

Per `docs/design/05-reference-platforms.md §2.2`:

> The Icicle is "RISC-V Linux"; the C3 is "RISC-V everything else."
> If the same `.merlin.o` produced by the GCC RISC-V toolchain runs
> on both (with profile differences encoded in the meta section),
> the cross-OS reusability claim is demonstrated end-to-end.

Concretely the sample-classifier blob in this directory is
identical to the one that would run under the Linux kernel/merlin/
loader on the MPFS Icicle Kit, modulo `.merlin.meta` profile tag.

---

## Assisted-by

Copilot-CLI:Claude-Opus
