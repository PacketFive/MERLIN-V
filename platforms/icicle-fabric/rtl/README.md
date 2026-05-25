# rtl/ — Verilog for the icicle-fabric MERLIN-V offload core

## Files

| File | Role |
|------|------|
| `merlin_top.v` | Top-level: AXI-Lite slave + memories + soft core |
| `axi_ctrl.v` | AXI-Lite slave; control regs + IMEM/DMEM upload windows |
| `merlin_imem.v` | 1 KiB instruction RAM (single-port BRAM model) |
| `merlin_dmem.v` | 1 KiB data scratchpad (byte-enable writes) |
# rtl/ — Verilog for the icicle-fabric MERLIN-V offload core

## Files

| File | Role |
|------|------|
| `merlin_top.v` | Top-level: AXI-Lite slave + memories + soft core |
| `axi_ctrl.v` | AXI-Lite slave; control regs + IMEM/DMEM upload windows |
| `merlin_imem.v` | 1 KiB instruction RAM (single-port BRAM model) |
| `merlin_dmem.v` | 1 KiB data scratchpad (byte-enable writes) |
| `merlin_core.v` | Soft-core wrapper; **simulation stub** (no real CPU) |
| `merlin_core_picorv32.v` | Soft-core wrapper instantiating PicoRV32 (synth-ready) |
| `vendor/picorv32/picorv32.v` | Vendored PicoRV32 (YosysHQ, ISC license) |

Both `merlin_core.v` and `merlin_core_picorv32.v` present the
**same external port list**, so `merlin_top.v` does not change
when you swap them.  The selection is done in the build:

- `cd ../tb && make`             — build with the stub (default)
- `cd ../tb && make picorv32`    — build with PicoRV32

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

### Stub model (`merlin_core.v`)

The stub is a 50-line model that decodes exactly the four
instructions used in the documented worked-example classifier
(`addi`, `lbu`, `beq`, `jalr`).  It is **not** synthesisable as a
real core, and it has a known BRAM-latency timing quirk that
makes each instruction effectively execute twice; the classifier
happens to land on the right result through that quirk.  The stub
is retained because its testbench (`tb/tb_merlin_top.v`) is the
fastest way to verify the AXI-Lite control path, the memories,
and the upload protocol in isolation, with a 5-cycle program run.

### Real soft core (`merlin_core_picorv32.v`)

The default real-core implementation is PicoRV32 (YosysHQ, single
file, ISC licence, vendored at `vendor/picorv32/picorv32.v`).
Configuration: RV32I (no `M`, no `C`, no `A`, no IRQ, no PCPI),
single-bus interface, `PROGADDR_RESET=0`, `STACKADDR=0x10000400`.

PicoRV32's unified `mem_valid`/`mem_ready` bus is adapted to the
split IMEM/DMEM ports of `merlin_top.v` by an FSM inside the
wrapper:

| Memory region | Address range | Behaviour |
|---------------|---------------|-----------|
| `IMEM`        | `0x0000_0000`..`0x0FFF_FFFF` | read-only; instruction fetches and read-only data reads |
| `DMEM`        | `0x1000_0000`..`0x1FFF_FFFF` | byte-enabled read/write scratchpad |
| `MMIO_HALT`   | `0x2000_0000`..`0x2FFF_FFFF` | write-only; latches `wdata` into `exit_value` and asserts `halted` |

Reads take three cycles in the wrapper (drive address → BRAM
latency → latch + ack) so PicoRV32 sees a ~5-cycle-per-instruction
sustained throughput on the shim+classifier (a 12-instruction
program runs in ~80 cycles).

Because real RISC-V has no implicit halt instruction, programs
running on `merlin_core_picorv32` are wrapped in a shim that
writes the result to the `MMIO_HALT` register and spins; the
`tb/tb_merlin_top_picorv32.v` testbench builds this shim
in-line (5 instructions of shim + 7 of classifier = 12 total).

### Other soft-core options (out of scope but tested elsewhere)

- **VexRiscv-Murax** (Scala-generated, ~3000 LUTs, more
  performant; supports `M`, `C`).  Drop-in feasible by writing a
  separate `merlin_core_vexriscv.v` wrapper analogous to the
  PicoRV32 one.  Not committed pending need.
- **Microchip MiV** (vendor IP, requires Libero subscription).

## Smoke testing

```bash
cd ../tb
make             # stub
make picorv32    # real PicoRV32 core
```

Both targets run the same two-packet smoke test (ETH/IPv4 +
ETH/RARP) and expect `[tb] all good`.

## Synthesis notes

- The BRAMs (`merlin_imem`, `merlin_dmem`) are written to map to
  PolarFire LSRAM blocks (1024×18 single-port).  Vivado-style
  inference; works on Libero's Synplify too.
- AXI-Lite handshake is 1-cycle on both channels; works at the
  150 MHz fabric clock the Icicle's FIC0 bus runs at.
- No clock-domain crossing in this design: the host AXI master
  on the MSS side runs synchronous to the FIC0 clock.
- PicoRV32 with the conservative config above (no MUL/DIV, no
  barrel shifter, no IRQ) synthesises to roughly 750–1100 LUTs
  on PolarFire; exact numbers depend on Synplify Pro options.
  See `BRINGUP.md` for the synthesis recipe.
