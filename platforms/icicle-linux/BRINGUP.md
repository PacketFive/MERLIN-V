# MPFS Icicle Kit Bring-up Procedure (Linux)

This document is the step-by-step recipe for taking an Icicle Kit
from out-of-box to a kernel that has the MERLIN-V module loaded and
the sample classifier running.

---

## 1. Prerequisites

### Hardware

- Microchip PolarFire SoC Icicle Kit
- USB-C cable for the FT4232 USB-UART bridge (ports for UART0 / UART1
  + JTAG)
- SD card (16 GiB or larger) for the rootfs
- Ethernet cable (optional, for `west flash` over the network)

### Software (Linux build host)

- riscv64 cross-toolchain (`riscv64-unknown-linux-gnu-gcc`) or
  Microchip's mpfs-dev SDK.
- A serial terminal program (`picocom`, `screen`, or `minicom`).
- Optional: Microchip's MPFS Discovery Kit toolset for HSS / U-Boot
  rebuilds. Most users start from Microchip's prebuilt images and
  only rebuild the Linux kernel + module.

References:
- Microchip MPFS Icicle Kit Yocto BSP:
  <https://github.com/polarfire-soc/meta-polarfire-soc-yocto-bsp>
- Upstream Linux RISC-V quickstart:
  <https://docs.kernel.org/arch/riscv/>

---

## 2. Boot Linux on the Icicle

1. Download Microchip's reference SD image and flash it to the SD
   card (`dd` or Balena Etcher).
2. Insert the SD card, connect USB-UART, set DIP switches to SD boot.
3. Power on; observe boot log on UART0:
   ```
   HSS    | Starting boot ...
   U-Boot | ...
   Linux  | ...
   ```
4. Log in (default credentials from Microchip's docs; change them).

Network: bring up `eth0` via DHCP; `ip addr` should show an address.

---

## 3. Build the MERLIN-V kernel module for the Icicle

You have two options:

### Option A — Cross-compile from a build host

```bash
# On your x86 dev box, with the riscv64 cross-toolchain installed:
cd /path/to/MERLIN-V/kernel/merlin

# Point at the running Icicle kernel's headers (rsync them off the
# board first, or use the matching kernel source tree):
make KDIR=/path/to/icicle-kernel-headers \
     CROSS_COMPILE=riscv64-unknown-linux-gnu- \
     ARCH=riscv
```

Copy the resulting `merlin.ko` to the board (`scp merlin.ko root@icicle:`).

### Option B — Build on the Icicle itself

The U54 cores are slow but capable; native build takes ~5 minutes.

```bash
# On the Icicle:
sudo apt-get install build-essential linux-headers-$(uname -r)
# (or yocto-equivalent: kernel-dev package from the BSP)
cd /path/to/MERLIN-V/kernel/merlin
make
```

---

## 4. Load the module

```bash
sudo insmod merlin.ko
dmesg | tail
# Expected:
#   merlin: MERLIN-V in-kernel JIT VM loaded (prototype)
#   merlin: device /dev/merlin minor=NNN
ls -l /dev/merlin
# crw------- 1 root root 10, NNN /dev/merlin
```

The pass-through JIT (`kernel/merlin/jit/pass_through.c`) is selected
because `CONFIG_MERLIN_JIT_RISCV=y` is the default on RISC-V.

---

## 5. Build and run the sample classifier

```bash
cd /path/to/MERLIN-V/platforms/icicle-linux/sample-classifier
make
sudo ./classifier
```

Expected:

```
classifier: opened /dev/merlin
classifier: loaded prog_id=1 verified_insns=7
classifier: ETH/IPv4 packet -> retval=2 (PASS)
classifier: ETH/RARP  packet -> retval=1 (DROP)
classifier: done
```

`dmesg | grep merlin` shows the kernel-side trace:

```
merlin: pass-through JIT: 28 bytes -> 0xffff80...
```

---

## 6. Troubleshooting

### `insmod: invalid module format`

The module was cross-compiled against the wrong kernel headers.
Rebuild against headers matching `uname -r` on the board exactly.
On Yocto/mpfs-dev images, `linux-headers-$(uname -r)` may not be
the right package name — see Microchip docs for your BSP version.

### `open /dev/merlin: Permission denied`

Run the sample with `sudo`, or `chmod 0666 /dev/merlin` (the module
defaults to `0600` since it grants CAP_BPF-equivalent privilege).

### `verifier rejected program` in dmesg

Build the kernel module with `CONFIG_MERLIN_LOG_LEVEL=2` (the
out-of-tree Makefile exposes this) and look at the verifier log
in the user-space `info.log_buf`.

### The kernel oopses

That's a bug; please file it. Capture `dmesg` and the exact RV64
bytecode that triggered the oops (`hexdump -C` of the `.text` section
of the offending `.merlin.o`).

---

## 7. Hot-reload demo (optional)

On RISC-V Linux the hot-reload path is identical to the eBPF
hot-reload path: open a new prog fd, ioctl `MERLIN_LINK_UPDATE`,
close the old fd. This is **Phase 2** work — `MERLIN_LINK_*`
returns `EOPNOTSUPP` in the current prototype.

---

## 8. What success looks like

The Icicle is "MERLIN-V capable" when:

- [ ] `merlin.ko` loads cleanly on the running kernel
- [ ] `/dev/merlin` is present and ioctlable
- [ ] Sample classifier returns 2/1 for IPv4/RARP packets
- [ ] dmesg shows the pass-through JIT path was taken
      (not the x86 host JIT)
- [ ] Verifier accepts every reference workload from
      `tools/testing/selftests/merlin/`

This is the same acceptance bar as the ESP32-C3 bring-up
(`platforms/esp32c3/BRINGUP.md §6`), but on RISC-V Linux rather
than Zephyr.
