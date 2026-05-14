# Lab 10 — Hardware Bring-up

Module: 6
Effort tier: L
Prerequisites: Lab 09.
Design doc: [`docs/design/05-reference-platforms.md`](../design/05-reference-platforms.md) §1–§2.

> **Hardware required.** This lab assumes access to *one* of:
>
> - ESP32-C3-DevKitM-1 (≈ US$ 8 board, USB-UART).
> - Microchip MPFS Icicle Kit (≈ US$ 500).
>
> If you have neither, the autograder accepts a Lab 09 QEMU-only
> submission with a 20-point ceiling. The full hardware path
> demonstrates the offload story end-to-end.

## Learning objectives

- Move a Zephyr application unchanged from QEMU to silicon.
- Flash and bring up a board with no prior experience.
- Diagnose a real-hardware bug that does not reproduce in QEMU.
- Quantify the runtime overhead of the BPF-V stack on a constrained
  MCU.

## Background reading

- Zephyr board docs:
  - `esp32_devkitm`: <https://docs.zephyrproject.org/latest/boards/espressif/esp32c3_devkitm/doc/index.html>
  - `mpfs_icicle_kit`: <https://docs.zephyrproject.org/latest/boards/microchip/mpfs_icicle_kit/doc/index.html>
- ESP32-C3 datasheet (chapters on RV32IMC core, memory map, IRAM).
- MPFS technical reference (E51 monitor + U54 cluster).

## Specification

You will:

1. Rebuild `runtime/zephyr/` for `esp32_devkitm` (Path A) **or**
   `mpfs_icicle_kit` (Path B).
2. Flash and boot.
3. Verify the Zephyr shell appears on the board's UART.
4. Load Lab 09's three sample programs over UART.
5. Run, measure, and report.

### Path A — ESP32-C3-DevKitM-1

- Toolchain: Zephyr SDK's `riscv32-zephyr-elf-gcc`.
- Build: `west build -p auto -b esp32_devkitm runtime/zephyr/`.
- Flash: `west flash --esp-baud-rate 460800`.
- Serial: 115200 8N1, USB-CDC.

### Path B — MPFS Icicle Kit

- Configure the kit for "Linux on U54 cluster, Zephyr on E51 monitor"
  per the kit's Hart Software Services (HSS) docs.
- Build: `west build -p auto -b mpfs_icicle_kit runtime/zephyr/`.
- Load via HSS console (TFTP or serial XMODEM).

## Tasks

### Task 1 — Build for hardware

Confirm a clean `west build` and a sensible image size.
Document size with `west build -t rom_report`.

### Task 2 — Flash and boot

Document, with photos or screenshots, the board lighting up with the
Zephyr shell over UART.

### Task 3 — Run prior labs' samples

Run `samples/blinky.bpfv.o` (Path A: blink the onboard LED via
`gpio_pin_toggle_dt`; Path B: drive a header pin) using `bpfvi load`
and `bpfvi run`.

### Task 4 — Hardware-only bug hunt

Pick **one** of the following anomalies you are very likely to hit
(or design a similar one):

- Cache or buffer alignment surprises that QEMU doesn't model.
- I-cache invalidation timing on a real pipeline.
- A peripheral helper that misbehaves under interrupt pressure.

Describe in `WRITEUP.md`:

- The symptom.
- The diagnostic steps (gdb-over-JTAG, `printk`, logic analyser).
- The fix.
- The reason QEMU didn't catch it.

### Task 5 — Measurements on hardware

Repeat Lab 09's boot-time bench on the board. Report:

- ELF receive over UART.
- Validate.
- Verify.
- Pass-through "JIT".
- First invocation.

Plot the result in a small table or chart in `WRITEUP.md`.

### Task 6 — Footprint

Report:

- Flash size of the Zephyr image with BPF-V runtime.
- Flash size of the same image *without* BPF-V (CMake option
  provided in the skeleton).
- RAM high-water from `west build -t rom_report` and
  `west build -t ram_report`.

## Deliverables

- A `WRITEUP.md` with the photos, measurements, and bug story.
- A `LOG_<board>.txt` capture of a complete shell session: boot,
  load, run, info.
- A `MEASUREMENTS.csv` for the lab's bench numbers.

## Rubric

| Criterion | Points |
| --------- | ------ |
| Builds for the chosen board cleanly | 10 |
| Boots and shell available on real serial | 15 |
| All three sample programs run on hardware | 25 |
| Hardware-only bug hunt write-up is concrete and convincing | 20 |
| Measurements reported with units and methodology | 15 |
| Footprint numbers reported | 5 |
| Writeup quality and AI attribution | 10 |
| **Total** | **100** |

(Software-only submission ceiling: 20 points, awarded only if the
hardware was demonstrably unavailable.)

## Common pitfalls

- ESP32-C3 boot-loader expectations: `esp_flasher` needs the right
  baud and the right partition map. Use Zephyr's defaults rather than
  ESP-IDF's.
- Icicle's HSS image vs U-Boot image confusion; read the HSS docs.
- Serial console at the wrong baud will *look* like the board is
  dead.

## What's next

Lab 11 closes the loop: a small XDP-V-style demonstrator running the
whole BPF-V stack you've built, with a measurement-driven writeup
that resembles a workshop paper section.
