# rtl/ — Verilog for the icicle-fabric MERLIN-V offload core

## Files

| File | Role |
|------|------|
| `merlin_top.v` | Top-level: AXI-Lite slave + memories + soft core |
| `axi_ctrl.v` | AXI-Lite slave; control regs + IMEM/DMEM upload windows |
| `merlin_imem.v` | 1 KiB instruction RAM (single-port BRAM model) |
| `merlin_dmem.v` | 1 KiB data scratchpad (byte-enable writes) |
| `merlin_core.v` | Soft-core wrapper; **stub** model for sim, replace for synth |

## AXI-Lite memory map (relative to the slave base)

| Offset | Name        | RW | Purpose |
|--------|-------------|----|---------|
| `0x000` | `CTRL`     | wo | bit0 = run, bit1 = soft_reset (active high) |
| `0x004` | `STATUS`   | ro | bit0 = halted, bit1 = running |
| `0x008` | `EXIT`     | ro | exit value (a0 at halt) |
| `0x010` | `IMEM_WSEL` | rw | next IMEM write index (in 32-bit words) |
| `0x014` | `IMEM_WDATA` | wo | write a word into IMEM at `IMEM_WSEL`; auto-increment |
| `0x018` | `DMEM_WSEL` | rw | next DMEM write index |
| `0x01c` | `DMEM_WDATA` | wo | write a word into DMEM at `DMEM_WSEL`; auto-increment |

## Loading and running an image

The host driver does:

1. Soft-reset the core (`CTRL = 0x2`).
2. Set `IMEM_WSEL = 0`.
3. Stream the verified `.text` bytes 4 at a time into `IMEM_WDATA`.
4. Set `DMEM_WSEL = 0` and stream the ctx buffer into `DMEM_WDATA`.
5. Release reset (`CTRL = 0x0`) and start (`CTRL = 0x1`).
6. Poll `STATUS` until `halted` is set.
7. Read `EXIT` for the program's return value.

## Soft-core choices

The stub in `merlin_core.v` is a 50-line model that decodes
exactly the four instructions used in the worked-example
classifier (`addi`, `lbu`, `beq`, `jalr`).  It is **not**
synthesisable as a real core.

For Libero / PolarFire synthesis, replace the body of
`merlin_core` with one of:

- **VexRiscv** (Scala-generated Verilog, rv32imc, ~3000 LUTs):
  generate the Verilog with the `Murax` / `GenSmallest` config
  from <https://github.com/SpinalHDL/VexRiscv>, then wrap it
  to match the imem/dmem port interface in this file.
- **PicoRV32** (single-file Verilog, rv32imc, ~750 LUTs):
  <https://github.com/YosysHQ/picorv32>.  Simpler ports; wire
  `mem_addr`/`mem_rdata`/`mem_wdata`/`mem_wstrb` to our
  `dmem_*` ports.  Use a separate instance for `imem` reads or
  multiplex on `mem_instr`.
- **Microchip MiV** (vendor IP, requires Libero subscription
  tier — least preferred but tested by Microchip on this fabric).

The wrapper's external interface stays the same regardless of
the chosen core; only the body of `merlin_core.v` changes.

## Smoke testing

```bash
cd ../tb
make
```

Runs the iverilog testbench end-to-end against this stub core.

## Synthesis notes

- The BRAMs (`merlin_imem`, `merlin_dmem`) are written to map to
  PolarFire LSRAM blocks (1024×18 single-port).  Vivado-style
  inference; works on Libero's Synplify too.
- AXI-Lite handshake is 1-cycle on both channels; works at the
  150 MHz fabric clock the Icicle's FIC0 bus runs at.
- No clock-domain crossing in this design: the host AXI master
  on the MSS side runs synchronous to the FIC0 clock.
