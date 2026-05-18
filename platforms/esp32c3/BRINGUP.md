# ESP32-C3-DevKitM-1 Bring-up Procedure

This document is the recipe for taking an out-of-box DevKitM-1, getting
Zephyr running on it, and loading + running a MERLIN-V program.

---

## 1. Prerequisites

### Hardware

- ESP32-C3-DevKitM-1 board
- USB-C cable
- (Optional) USB-UART bridge if your DevKitM doesn't enumerate as a
  serial device under your OS (most do via the built-in USB Serial/JTAG)

### Software (Linux host)

```bash
# Zephyr SDK 0.16.x or newer with riscv32 toolchain.
# https://github.com/zephyrproject-rtos/sdk-ng/releases
export ZEPHYR_SDK_INSTALL_DIR=$HOME/zephyr-sdk-0.16.5

# Zephyr base
git clone https://github.com/zephyrproject-rtos/zephyr ~/zephyrproject/zephyr
cd ~/zephyrproject
west init -l zephyr
west update

# Espressif HAL (provides ESP32-C3 board support)
west blobs fetch hal_espressif

# Python deps
pip3 install --user -r ~/zephyrproject/zephyr/scripts/requirements.txt

export ZEPHYR_BASE=$HOME/zephyrproject/zephyr
```

Verify:

```bash
west boards | grep esp32c3
# should list esp32c3_devkitm
```

---

## 2. First flash — hello world

Sanity-check the toolchain and board before bringing MERLIN-V in:

```bash
cd ~/zephyrproject/zephyr
west build -b esp32c3_devkitm -p always samples/hello_world
west flash
west espressif monitor
```

If you see `*** Booting Zephyr OS … Hello World! esp32c3_devkitm`,
the toolchain + flash + serial path is working. Ctrl-] to exit
the monitor.

---

## 3. Build the MERLIN-V sample-classifier

From the **MERLIN-V** repo root:

```bash
west build -b esp32c3_devkitm \
    -p always platforms/esp32c3/sample-classifier \
    -- -DEXTRA_ZEPHYR_MODULES="$(pwd)/zephyr/merlin"

west flash
west espressif monitor
```

Expected:

```
*** Booting Zephyr OS build … ***
merlin: runtime initialised (slots=4, max_bytes=16384, stack=256)
classifier: boot on esp32c3 (RV32IMC, no MMU)
classifier: loaded id=1 insns=N
classifier: synthetic packet ETH/IPv4/TCP → verdict=2 (PASS)
classifier: synthetic packet ETH/ARP    → verdict=1 (DROP)
classifier: done
```

---

## 4. Hot-reload demo (optional)

The DevKitM-1 has a single USB endpoint that combines UART + JTAG.
For a "no-reboot" replace demo, build a second blob (different
verdict logic) and tweak `main.c` to load it on a button-press or
timer. The runtime path is:

```c
merlin_prog_unload(prog_v1);
rc = merlin_prog_load(blob_v2, ..., &info, &prog_v2);
```

The internal `merlin_runtime_install()` re-`k_malloc`s an
executable buffer and re-flushes the I-cache. No reboot.

---

## 5. Troubleshooting

### Board not detected

- Check that `dmesg` shows `cp210x` or `ch341` driver attaching to
  the new `/dev/ttyUSB*` node when you plug the board in.
- On Linux the user needs to be in the `dialout` group.

### `esptool: invalid header` after `west flash`

- Hold the BOOT button while plugging in USB to enter download
  mode manually, then release after `west flash` begins.

### Verifier rejects the classifier

- Build with `CONFIG_MERLIN_LOG_LEVEL=2` and check the serial
  console; the verifier prints a per-instruction reason.

### `merlin: runtime initialised` never appears

- Check that `CONFIG_MERLIN=y` is in the project `prj.conf`.
- Check that the `EXTRA_ZEPHYR_MODULES` path points at the
  *parent* directory of `zephyr/merlin/`, not the module itself.

---

## 6. What success looks like

The board is "MERLIN-V capable" when:

- [ ] A signed `.merlin.o` loads from a `west flash`'d image
- [ ] The verifier accepts it on the rtos-rv32 profile
- [ ] Pass-through execution produces the expected verdict
- [ ] Hot-reload swaps the program without reboot

This is the **same** acceptance bar as the MPFS Icicle Kit
(`platforms/icicle/`, future). The point of the C3 bring-up is to
demonstrate that the criterion is hardware-portable.
