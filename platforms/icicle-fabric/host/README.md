# host/ — userland AXI-Lite loader

`merlin-fabric-load` opens a UIO device exposing the fabric core's
AXI-Lite slave, uploads a verified MERLIN-V `.text` section into the
fabric IMEM, optionally uploads a context buffer into the fabric
DMEM, releases the core, and reads back the result.

## Build

On a riscv64 build host (Icicle native):

```bash
make
```

Cross-compile from an x86 dev box:

```bash
make CROSS_COMPILE=riscv64-unknown-linux-gnu-
```

## Devicetree fragment

For the kernel to expose the AXI-Lite slave as `/dev/uio0`, add a
node to your Icicle devicetree's `&fic0` (or wherever the AXI-Lite
master lives):

```
&fic0 {
    merlin_fabric: merlin-fabric@40000000 {
        compatible = "generic-uio";
        reg = <0x4000_0000 0x1000>;
        status = "okay";
    };
};
```

Then build the kernel with `CONFIG_UIO_PDRV_GENIRQ=y` (or m), and
the device will appear as `/dev/uio0`.

## Usage

```bash
# Extract the .text section bytes from a verified .merlin.o:
riscv64-unknown-linux-gnu-objcopy -O binary \
    --only-section=.text.merlin.filter.classifier \
    classifier.merlin.o classifier.text.bin

# Push into the fabric core:
sudo ./merlin-fabric-load classifier.text.bin
```

Expected output:

```
fabric-load: opened /dev/uio0, text=28 bytes
fabric-load: ETH/IPv4 -> 2  (PASS)
fabric-load: ETH/RARP -> 1  (DROP)
```

## Integration with `merlin.ko`

This standalone loader is a development tool.  The production flow
extends `kernel/merlin/loader.c` to:

1. Verify the `.merlin.o` on the host CPU (existing path).
2. Detect that the destination JIT target is `MERLIN_PROFILE_FABRIC`.
3. Push the verified bytes through this same AXI-Lite interface
   from kernel space, instead of returning a JIT function pointer.

The user-space ABI (`MERLIN_PROG_TEST_RUN`) then runs the program
on the fabric core transparently.  This is the
`platform-icicle-fabric` capstone, separate from the
`merlin-fabric-load` standalone scaffolding here.
