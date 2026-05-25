# MPFS Icicle Kit — Fabric Soft-Core Bring-up

This is the procedure for taking an Icicle Kit from "Linux on U54
runs MERLIN-V via `merlin.ko`" to "Linux on U54 hands a verified
MERLIN-V image to an RV32 soft core in the PolarFire fabric, which
executes it natively."

---

## 1. Prerequisites

### Hardware
- MPFS Icicle Kit + USB-UART cable + SD card (set up per
  `platforms/icicle-linux/BRINGUP.md` first — this guide assumes
  Linux is already booting and `merlin.ko` is already loadable).
- Optional: a FlashPro / Embedded FlashPro probe for direct
  bitstream loading.

### Build host
- **Microchip Libero SoC** (current version: 2024.x; free-tier
  works for the MPFS250T-FCVG484EES on the Icicle Kit, but the
  `MPFS_DISCOVERY_KIT` board file is needed).  Download from
  Microchip; not in this repo.
- **iverilog + verilator** (for simulation; pre-installable on any
  Debian/Ubuntu/Fedora host).

---

## 2. Pick the soft core

`rtl/merlin_core.v` is a **simulation stub** retained for fast
AXI-Lite / memory-protocol smoke testing.  It is not synthesisable.
For real hardware, use **PicoRV32** via the wrapper that already
ships in the tree.

### Option A — PicoRV32 (default, vendored)

PicoRV32 is vendored at
`rtl/vendor/picorv32/picorv32.v` (YosysHQ, ISC licence) and is
wrapped by `rtl/merlin_core_picorv32.v`.  No additional download
or codegen is needed; the wrapper presents the same external
ports as the stub so `merlin_top.v` is unchanged.

The wrapper configures PicoRV32 for RV32I only (no `M`, no `C`,
no `A`, no IRQ, no PCPI), `PROGADDR_RESET=0`,
`STACKADDR=0x10000400`.  Programs are wrapped in a tiny shim
that writes the result to the MMIO halt register (see
`rtl/README.md` for the address map).

Smoke test:

```bash
cd tb
make picorv32
```

Expected:

```
[tb] ETH/IPv4: exit=2 (PASS, expected 2, polls=19)
[tb] ETH/RARP: exit=1 (PASS, expected 1, polls=18)
[tb] all good (picorv32 core)
```

### Option B — VexRiscv-Murax

Not vendored in this tree (Scala codegen, larger).  Bring it in if
you need the `M`/`C` extensions or the lower CPI:

```bash
git clone https://github.com/SpinalHDL/VexRiscv
cd VexRiscv
sbt "runMain vexriscv.demo.GenSmallest"
# Produces VexRiscv.v; ~3000 LUTs on PolarFire.
```

Then write a `rtl/merlin_core_vexriscv.v` wrapper analogous to
`merlin_core_picorv32.v` (map `iBus_cmd`/`iBus_rsp` to `imem_*`
and `dBus_cmd`/`dBus_rsp` to `dmem_*`).  A `make vexriscv` target
in `tb/Makefile` can follow the PicoRV32 pattern.

### Option C — Microchip MiV (vendor IP)

Available through Libero's IP Catalogue (free for the Icicle
Kit's chip).  Easiest to drop into SmartDesign; least open.

---

## 3. iverilog smoke (verify the data path)

Run both testbenches before touching Libero:

```bash
cd tb
make             # stub  (validates AXI-Lite + memories quickly)
make picorv32    # real RV32I core through PicoRV32
```

Expected (stub):

```
[tb] ETH/IPv4: exit=2 (PASS, expected 2, cycles=5)
[tb] ETH/RARP: exit=1 (PASS, expected 1, cycles=4)
[tb] all good
```

Expected (PicoRV32):

```
[tb] ETH/IPv4: exit=2 (PASS, expected 2, polls=19)
[tb] ETH/RARP: exit=1 (PASS, expected 1, polls=18)
[tb] all good (picorv32 core)
```

Both validate:
- AXI-Lite slave handshakes (one-cycle aw+w fire; bvalid 1 cycle later).
- IMEM upload via auto-incrementing `IMEM_WSEL`.
- DMEM upload via auto-incrementing `DMEM_WSEL`.
- Core boots from address 0 on `CTRL = 0x1`.

The PicoRV32 testbench also exercises:
- Real RV32I instruction decode / execution.
- Multi-cycle bus handshake with the BRAM-latency-aware adapter.
- The MMIO halt register (write to `0x20000000` halts + latches).

---

## 4. Synthesise with Libero

```bash
cd libero
/opt/microchip/Libero_SoC/bin/libero SCRIPT:build.tcl
```

This creates a project in `build/libero/`, runs synth + P&R +
bitgen.  Output: `build/libero/designer/merlin_fabric/merlin_fabric.bit`.

For the MSS-to-fabric AXI wiring (which is interactive in
SmartDesign, not TCL), see `libero/README.md`.

Target slack at 150 MHz: positive across all paths.  The
single-cycle ALU paths and the BRAM-fed memories are
comfortable at this frequency on the Icicle's speed grade.

---

## 5. Program the FPGA

Boot the Icicle Linux first (per `platforms/icicle-linux/BRINGUP.md`),
then over JTAG or over the in-system programming USB path:

```bash
# Using the Microchip command-line programmer:
mpfsBootmodeProgrammer \
    --bitstream merlin_fabric.bit \
    --device   PolarFireSoC \
    --target   icicle-kit
```

Alternative: copy the `.bit` file onto the SD card and use the
HSS payload-update path to reprogram the FPGA design slot at boot.

---

## 6. Wire up the kernel-side device

Add a node to the Icicle's devicetree (`mpfs-icicle-kit.dts` or
similar) so the AXI-Lite slave appears at a known address and
the UIO driver attaches:

```
&fic0 {
    merlin_fabric: merlin-fabric@40000000 {
        compatible = "generic-uio";
        reg        = <0x4000_0000 0x1000>;
        status     = "okay";
    };
};
```

Rebuild + install the dtb; reboot.  After boot, `/dev/uio0` should
appear.

---

## 7. End-to-end demo

```bash
# Extract the .text bytes from a verified .merlin.o:
riscv64-unknown-linux-gnu-objcopy -O binary \
    --only-section=.text.merlin.filter.classifier \
    classifier.merlin.o classifier.text.bin

# Run the userland loader:
cd host
make
sudo ./merlin-fabric-load classifier.text.bin
```

Expected:

```
fabric-load: opened /dev/uio0, text=28 bytes
fabric-load: ETH/IPv4 -> 2  (PASS)
fabric-load: ETH/RARP -> 1  (DROP)
```

---

## 8. What success looks like

The Icicle is "MERLIN-V fabric capable" when:

- [ ] iverilog testbench passes against the stub
- [ ] Bitstream synthesises with a real soft core (VexRiscv / PicoRV32 / MiV)
- [ ] FPGA is programmed; `/dev/uio0` appears in Linux
- [ ] The end-to-end loader returns 2 for IPv4, 1 for RARP
- [ ] `dmesg` shows the AXI-Lite slave registered correctly

This is the strongest demonstration of the MERLIN-V thesis: the
same `.text` bytes that ran natively on the U54 cores
(`platforms/icicle-linux/`) and on Zephyr/ESP32-C3
(`platforms/esp32c3/`) now run natively on **the student's own
soft core** — on the same die as the host CPU, with the host
CPU acting as the verifier-trusted loader.

---

## 9. Troubleshooting

### iverilog testbench hangs

Most likely a re-introduced race in the AXI-Lite slave.  The
slave assumes one-cycle handshake (aw + w fire together; b 1
cycle later); the testbench task `axi_write` matches this.  If
you replace the slave with a Xilinx/Vivado-style multi-cycle
slave, update `axi_write` to match.

### Synthesis fails on inferred BRAM

PolarFire LSRAM is single-port + registered output.  Both
`merlin_imem.v` and `merlin_dmem.v` write that way.  If the tool
fails to infer LSRAM, check `BRAM` and `RAM_BLOCK` attributes in
the Libero log and tag the `mem` array explicitly with
`(* ram_style = "block" *)`.

### Soft core won't fit

Drop the FPU (it's not in any rtos-rv32 MERLIN-V profile).
PicoRV32 fits easily (~750 LUTs) at the cost of multi-cycle
execution.  If even that doesn't fit, the issue is unlikely the
core itself — check that the `MPFS250T_FCVG484` device file is
in use (smaller die variants exist).
