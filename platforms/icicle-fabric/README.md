# platforms/icicle-fabric/ — MERLIN-V on a PolarFire soft RV32 core

Status: **scaffolding** (RTL + host-side; HW bring-up pending)
Cross-reference: `docs/design/05-reference-platforms.md §1.3`

---

## What this proves

The MPFS Icicle Kit hosts both a hardened RISC-V Linux SoC (4× U54)
**and** a PolarFire FPGA fabric on the same die.  This platform
brings up an additional RISC-V core — a soft core synthesised into
the fabric — and uses it as a **MERLIN-V offload engine**.

The picture:

```
   ┌──────────────────────┐         ┌─────────────────────────┐
   │  Linux on U54 cores  │         │  PolarFire FPGA fabric  │
   │                      │  AXI    │                         │
   │  merlin.ko module    │ ──────▶ │   axi_ctrl ────▶ imem   │
   │  merlin-fabric-load  │         │      │                  │
   │                      │         │      ▼                  │
   │  (verifier runs HERE)│         │   RV32IMC soft core     │
   │                      │         │      ▲                  │
   │  retval ◀── AXI ───  │ ◀────── │   dmem  ◀────── ctx     │
   └──────────────────────┘         └─────────────────────────┘
       host CPU                          fabric core
```

The verifier and loader run **on Linux** (the U54 cluster); the
verified bytecode is then pushed over an AXI bridge into the
fabric core's instruction memory and the core is released from
reset.  The fabric core executes MERLIN-V bytecode **natively at
the instruction level** — no JIT, no translation, no
interpretation, because the bytecode *is* the soft core's ISA.

This is the simplest concrete demonstration of the MERLIN-V design
thesis from `docs/design/00-overview.md §2`:

> RISC-V hardware accelerators run verified MERLIN-V images
> natively at the instruction level — 1:1 with no translation.

---

## Contents

```
platforms/icicle-fabric/
├── README.md              this file
├── BRINGUP.md             step-by-step bring-up procedure
│
├── rtl/                   Verilog RTL (vendor-neutral, iverilog-smoke-tested)
│   ├── README.md          RTL overview + memory map
│   ├── merlin_top.v       top-level: core + memories + AXI ctrl
│   ├── merlin_core.v      soft-core wrapper (instantiates VexRiscv / PicoRV32)
│   ├── merlin_imem.v      single-port instruction RAM (BRAM)
│   ├── merlin_dmem.v      single-port data scratchpad (BRAM)
│   └── axi_ctrl.v         AXI-Lite slave: ctrl/status + image upload
│
├── tb/                    iverilog-based testbench
│   ├── Makefile
│   ├── tb_axi_ctrl.v      smoke: AXI-Lite reg read/write
│   └── tb_merlin_top.v    end-to-end: upload image + run + read retval
│
├── libero/                Microchip Libero project skeleton
│   ├── README.md          how to import the RTL into Libero
│   ├── build.tcl          headless TCL build (synth + P&R + bitstream)
│   └── constraints/
│       ├── pinmap.pdc     PolarFire pin assignments (placeholder)
│       └── timing.sdc     timing constraints
│
├── host/                  Linux user-space loader
│   ├── README.md
│   ├── Makefile
│   └── merlin-fabric-load.c   open /dev/uio0, upload + run a .merlin.o
│
└── sample-classifier/     reuse the icicle-linux/esp32c3 classifier blob
    ├── README.md
    └── src/
        └── classifier_blob.c   symlinked from icicle-linux's blob
                                 (same RV32 .text bytes; Elf32 wrapper)
```

The soft core choice is **deliberately not pinned** in this
directory; `rtl/merlin_core.v` is a thin wrapper around a generic
core interface (instruction-bus + data-bus, no AXI master inside
the core, so it ports to PicoRV32, VexRiscv-minimal,
SERV, CV32E40P, or Microchip's MiV).  See `BRINGUP.md §3` for the
two recommended options.

---

## What this is for in pedagogy

This platform is the natural top of the MERLIN-V academic stack:

```
   labs 02-04  →  user-space interp + verifier      (one machine)
   labs 06-07  →  pass-through + host JITs           (one machine)
   labs 08-09  →  in-kernel runtime (Linux + Zephyr) (one CPU)
       │
       ▼
   icicle-fabric (this directory)
                 →  MERLIN-V image runs on a CPU
                    THE STUDENT DESIGNED IN VERILOG,
                    on the same die as the loader CPU.
```

The book treats it as the final chapter (`book-write-part-6`):
"the multiplexer to the soft core to the in-kernel JIT VM is one
continuous engineering line."  Students who finished Lab 11 with
an RV32 core in Verilog (from the Onur-Mutlu-style hardware
preliminary course) can synthesise their own core here, not
necessarily a vendor IP.

---

## Status

- [x] Scaffolding (this directory)
- [x] BRINGUP.md
- [x] Verilog RTL (vendor-neutral, iverilog-smoke-tested)
- [x] AXI-Lite control register block + image-upload window
- [x] iverilog testbench (smoke + end-to-end)
- [x] Host-side userland loader stub
- [x] Libero TCL build script + constraint placeholders
- [ ] Synth + P&R verified on real Libero install (needs Microchip Libero SoC)
- [ ] Bitstream loaded onto an Icicle Kit (needs hardware)
- [ ] End-to-end demo: classifier_blob.c runs on the soft core,
      verdict read back by Linux

---

## Assisted-by

Copilot-CLI:Claude-Opus
