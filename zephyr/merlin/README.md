# zephyr/merlin/ — MERLIN-V Zephyr runtime (out-of-tree prototype)

Status: **prototype**
Cross-references: `docs/design/05-reference-platforms.md §6`,
`docs/design/02-isa-and-bytecode.md §1` (merlin-rtos-rv32 profile)

---

## What this is

A Zephyr [out-of-tree module](https://docs.zephyrproject.org/latest/develop/modules.html)
that brings MERLIN-V to RISC-V MCUs running Zephyr. On a RISC-V target
(ESP32-C3, MPFS Icicle E51, PolarFire fabric soft cores, …) verified
RISC-V bytecode loaded at runtime executes pass-through — no JIT translation,
no per-instruction overhead.

The runtime exposes four entry points:

```c
int  merlin_init(void);
int  merlin_prog_load(blob, blob_len, attr, info, prog);
int  merlin_prog_run(prog, ctx, retval);
int  merlin_prog_unload(prog);
```

…plus a helper-registration API for application-side helpers.

See `include/merlin/merlin.h` for the public API.

---

## Source layout

```
zephyr/merlin/
├── zephyr/module.yml      # Zephyr module manifest
├── CMakeLists.txt          # zephyr_library glue
├── Kconfig                 # CONFIG_MERLIN_* knobs
├── include/merlin/
│   └── merlin.h            # Public API
├── src/
│   ├── merlin_internal.h   # Internal types
│   ├── decode.c            # RV32 decoder (cut-down kernel port)
│   ├── verifier.c          # Abstract-interp verifier (rtos-rv32 profile)
│   ├── loader.c            # ELF parser (Elf32, packed structs)
│   ├── runtime.c           # Slot table + I-cache flush + run path
│   ├── dispatch.c          # Helper registry
│   └── helpers_sample.c    # Built-in helpers (printk, uptime, rng, gpio_*, yield)
├── samples/
│   └── hello-merlin/       # End-to-end sample
│       ├── CMakeLists.txt
│       ├── prj.conf
│       ├── sample.yaml     # west / Twister harness config
│       └── src/
│           ├── main.c
│           └── sample_blob.c  # Hand-rolled Elf32 with addi + ret
└── host_smoke/             # libc-host smoke test (decoder + verifier)
    ├── Makefile
    └── smoke.c
```

---

## Building the sample

```bash
# from the MERLIN-V repo root, with Zephyr SDK + west on PATH:
west build -b esp32c3_devkitm \
    -p always zephyr/merlin/samples/hello-merlin \
    -- -DEXTRA_ZEPHYR_MODULES=$(pwd)/zephyr/merlin
west flash
west espressif monitor    # or `picocom /dev/ttyUSB0 -b 115200`
```

Expected output on the device console:

```
merlin: runtime initialised (slots=4, max_bytes=16384, stack=256)
merlin-hello: boot
merlin-hello: loaded id=1 insns=2
merlin-hello: prog returned 42
merlin: hello-merlin done
```

---

## Host smoke test (decoder + verifier)

The runtime sources are CI-checkable without a cross-compiler. The
`host_smoke/` target compiles `decode.c` + `verifier.c` + the sample
blob against plain libc:

```bash
cd zephyr/merlin/host_smoke
make
./smoke
```

Output:

```
[smoke] sample_blob size = 229
[smoke] found section '.text.merlin.filter.hello' size=8
[smoke] pc=0 02a00513  ALU_IMM rd=x10 rs1=x0 rs2=x10 imm=42
[smoke] pc=4 00008067  JALR rd=x0 rs1=x1 rs2=x0 imm=0
[smoke] verifier: insns=2 rejected=0
[smoke] PASS
```

This validates:
- The sample blob's hand-rolled Elf32 parses correctly
- The decoder recognises both instructions
- The verifier accepts the program (no back-edges, no forbidden classes,
  no untyped loads/stores)

---

## Design decisions

### Single ISA, no JIT

The runtime is RISC-V-only. On RISC-V targets verified bytecode is
native, so the "JIT" is just `memcpy + sys_cache_instr_invd_range`.
On non-RISC-V Zephyr targets, `merlin_prog_run()` returns
`MERLIN_ERR_NOTSUP`. This deliberately diverges from the Linux runtime,
which carries an x86_64 host JIT for development on workstations.

Rationale: Zephyr's deployment target is the device; the device is
RISC-V by design choice (rtos-rv32 profile); workstation execution of
Zephyr-targeted MERLIN-V images is a CI concern that the **Linux**
runtime + the Linux x86_64 JIT already cover.

### Static slot table

`CONFIG_MERLIN_MAX_PROGS` (default 4) statically reserves the prog
table. This avoids unbounded heap fragmentation in long-running
firmware. Each slot holds the verified bytecode buffer + ~80 bytes of
metadata.

### Heap text buffer (no dedicated RX section)

We `k_malloc` the text buffer and rely on the absence of W^X enforcement
on common RISC-V MCUs without an MMU (ESP32-C3, E51 monitor core, FPGA
soft cores). On targets with W^X, a future patch will add a dedicated
`.merlin_text` linker section.

### Verifier is the same algorithm as Linux

The abstract-interpretation verifier is a strict subset of the Linux
kernel verifier (`kernel/merlin/verifier.c`). Same rejection criteria,
same domain (`Unknown / Const / PtrCtx / PtrStack / PtrHelperRet`).
This is the design's first-class guarantee: the verifier verdict on a
given `.merlin.o` is identical on Linux and Zephyr.

### Helper ABI matches Linux

`uint64_t fn(a0..a5)` with helper id in `a7`. The verifier's allowlist
bitset gates which helper ids each program type may call. Built-in
helpers in `helpers_sample.c` (printk, uptime, rng, gpio_set/get, yield)
register at `SYS_INIT(APPLICATION)`.

---

## Out-of-scope (prototype)

- C extension (16-bit) instructions: decoder marks them `INSN_UNSUPPORTED`;
  verifier rejects. Phase 2.
- Bounded-loop helper. Phase 2.
- Verifier CFG joins / widening. Phase 2.
- Multi-section ELF support (currently first `.text.merlin.*` only).
- AMO / atomic instructions: `INSN_UNSUPPORTED`. Phase 2 for boards
  with the A extension.

---

## Assisted-by

Copilot-CLI:Claude-Opus
