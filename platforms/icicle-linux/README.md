# platforms/icicle-linux/ — MERLIN-V on the MPFS Icicle Kit (Linux)

Status: **bring-up scaffolding**
Cross-reference: `docs/design/05-reference-platforms.md §1.1`

---

## Target board

- **Board**: Microchip PolarFire SoC Icicle Kit
  (`MPFS250T-FCVG484EES`)
- **SoC**: 4× SiFive U54 (RV64GC) + 1× E51 monitor core (RV64IMAC)
- **RAM**: 2 GiB LPDDR4 (board-dependent)
- **MERLIN-V profile**: `merlin-linux-rv64` (`rv64imac_zicsr_zifencei`)
  - F/D explicitly **disabled** for in-kernel programs even though
    the U54 cores have them; user space may use FP normally.
- **Verifier profile**: `linux-rv64/default`
- **OS**: Upstream Linux 6.x (or the Microchip mpfs-dev Yocto BSP)
- **Module loaded**: `kernel/merlin/merlin.ko` (the out-of-tree
  module that ships with this project)

---

## What this directory contains

```
platforms/icicle-linux/
├── README.md              this file
├── BRINGUP.md             step-by-step bring-up instructions
├── sample-classifier/
│   ├── Makefile
│   ├── src/
│   │   ├── main.c              user-space driver: open /dev/merlin,
│   │   │                        ioctl prog-load + test-run
│   │   ├── classifier_blob.c   hand-rolled Elf64 of the RV64 classifier
│   │   └── classifier_src.S    readable RV64 source
│   └── sample.yaml         test harness manifest (kselftest-style)
└── debug/
    └── dmesg-tap.sh        helper: tap dmesg with merlin: filter
```

`sample-classifier` is the bring-up flagship for the Linux side.
It mirrors `platforms/esp32c3/sample-classifier/` line-for-line but
targets the **kernel module**: the user-space test program opens
`/dev/merlin`, ioctls `MERLIN_PROG_LOAD`, then `MERLIN_PROG_TEST_RUN`.

The RV64 classifier and the RV32 classifier on the C3 are
**semantically the same program**:

> Load `pkt[12]` (EtherType high byte). If it's 0x08 (IPv4 family),
> return 2 (PASS). Otherwise return 1 (DROP).

— but encoded for the rv64 profile rather than rv32. This is the
"same `.merlin.o` runs on Icicle and on the C3" claim made concrete
(modulo profile-dependent encoding differences).

---

## Quick start

```bash
# 1. Build the kernel module (against the running rv64 kernel headers).
cd /path/to/MERLIN-V/kernel/merlin
make

# 2. Load the module.
sudo insmod merlin.ko
ls -l /dev/merlin       # should appear

# 3. Build and run the sample.
cd /path/to/MERLIN-V/platforms/icicle-linux/sample-classifier
make
sudo ./classifier
```

Expected output:

```
classifier: opened /dev/merlin
classifier: loaded prog_id=1 verified_insns=N
classifier: ETH/IPv4 packet -> retval=2 (PASS)
classifier: ETH/RARP  packet -> retval=1 (DROP)
classifier: done
```

If you also have a serial console attached, `dmesg | grep merlin`
shows the kernel-side load + verifier traces:

```
merlin: MERLIN-V in-kernel JIT VM loaded (prototype)
merlin: device /dev/merlin minor=N
merlin: pass-through JIT: 28 bytes -> 0x...
```

See `BRINGUP.md` for the full step-by-step including kernel build,
Yocto vs. upstream choice, and serial-console wiring.

---

## What this platform proves

Per `docs/design/00-overview.md` and §1.1 of the reference-platforms
doc:

> On a RISC-V Linux host, the verified MERLIN-V image *is* the
> executable: the "JIT" reduces to a pass-through step (verify →
> relocate → I-cache flush → jump), hardware-native at the instruction
> level — 1:1 with no translation.

This board is where that claim becomes concrete. The kernel module's
pass-through JIT path (`kernel/merlin/jit/pass_through.c`) is taken,
not the x86_64 host JIT.

---

## Status

- [x] Scaffolding (this directory)
- [x] BRINGUP.md
- [x] Sample-classifier user-space program
- [x] Hand-rolled RV64 ELF blob (host-validated)
- [ ] Tested on real hardware (requires Icicle Kit + serial console)
- [ ] Throughput / verifier-time baselines (eval-plan W1..W5)

---

## Assisted-by

Copilot-CLI:Claude-Opus
