# libero/ — Microchip Libero SoC project skeleton

Status: **TCL build script + constraint placeholders.**
Real bitstream generation requires Microchip Libero SoC
(commercial / free-tier; not in this repo) on a board-connected
build host.

## Files

| File | Purpose |
|------|---------|
| `build.tcl` | Headless build script: creates project, adds RTL, runs synth + P&R + bitgen |
| `constraints/pinmap.pdc` | Pin assignments (placeholder; mostly empty because MSS→fabric AXI is internal) |
| `constraints/timing.sdc` | 150 MHz fabric clock constraint |

## How to use

On a host with Libero SoC installed:

```bash
/opt/microchip/Libero_SoC/bin/libero SCRIPT:build.tcl
```

Or open Libero interactively and source the script from the TCL
console.

The output is `build/libero/designer/merlin_fabric/merlin_fabric.bit`,
which you load onto the Icicle Kit using `mpfsBootmodeProgrammer`
or the Microchip programmer GUI.

## Why this is a placeholder

The actual MSS-to-fabric AXI-Lite wiring is configured through
Libero's **MSS Configurator** GUI, not through TCL or `.pdc`.  In a
real project, you:

1. Open the MSS Configurator.
2. Enable FIC0 as an "AXI-Lite master" port.
3. Place the `merlin_top` module in the SmartDesign canvas.
4. Wire the FIC0 master to `merlin_top`'s AXI-Lite slave.
5. Compile.

The TCL script here automates the parts that can be automated
(project creation, RTL import, constraint application, run
sequence); the SmartDesign step is interactive.  For the
production setup, capture the SmartDesign canvas as a `.sdr`
file and add `import_files -sdr ...` lines to `build.tcl`.

## Soft-core choice for synthesis

Before running this script, replace the body of
`../rtl/merlin_core.v` with a real soft core.  See
`../rtl/README.md` for the two recommended options (VexRiscv,
PicoRV32) and how to wrap each one to match the imem/dmem port
interface.
