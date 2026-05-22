# sample-classifier/ — same bytecode, on a soft core

This directory does **not** contain a separately-built blob.
Instead it references the existing `platforms/icicle-linux/
sample-classifier/src/classifier_blob.c`: the SAME bytecode runs
on the U54 cores via `merlin.ko` AND on the fabric soft core via
this directory's `host/merlin-fabric-load`.

That's the whole point: there is one `.merlin.o`, and it runs in
two places on the same die.

## Files referenced

- `../../icicle-linux/sample-classifier/src/classifier_blob.c`
  --- the Elf64 wrapper used by the kernel-module path.
- `../../icicle-linux/sample-classifier/src/classifier_src.S`
  --- the readable RV64 source.

For the fabric path, the loader (`host/merlin-fabric-load.c`)
extracts just the 28 raw `.text` bytes and pushes them into IMEM:

```
   ┌─────────────────────────────┐
   │  classifier_blob.c          │  (one source of truth)
   │     ↓                        │
   │  Elf64 wrapper (326 bytes)   │
   └───────┬───────────────┬──────┘
           │               │
           ▼               ▼
   [Linux kernel       [fabric loader
    module path]        AXI-Lite path]
    merlin.ko           merlin-fabric-load
    runs on U54         runs on soft core
```

## Extracting just `.text` from the Elf64 wrapper

If you want the raw 28 bytes outside the Elf wrapper:

```bash
riscv64-unknown-linux-gnu-objcopy -O binary \
    --only-section=.text.merlin.filter.classifier \
    classifier.merlin.o classifier.text.bin
hexdump -C classifier.text.bin
```

Output:
```
00000000  83 45 c5 00 13 06 80 00  63 84 c5 00 13 05 10 00  |.E......c.......|
00000010  67 80 00 00 13 05 20 00  67 80 00 00              |g..... .g...|
```

— the same 28 bytes hand-rolled in the Elf32 blob in
`platforms/esp32c3/sample-classifier/src/classifier_blob.c`.
