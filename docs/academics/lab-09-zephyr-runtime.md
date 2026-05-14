# Lab 09 — Zephyr Runtime

Module: 6
Effort tier: M
Prerequisites: Lab 06 (pass-through JIT — Lab 09 reuses it without
the Lab 08 kernel module).
Design doc: [`docs/design/05-reference-platforms.md`](../design/05-reference-platforms.md) §5.

## Learning objectives

- Build a Zephyr application that includes BPF-V runtime code as a
  module.
- Port the user-space pass-through path to a no-`malloc`, no-`mmap`
  environment.
- Load a BPF-V image over UART into a Zephyr device and run it from
  the Zephyr shell.

## Background reading

- Zephyr docs: <https://docs.zephyrproject.org/latest/>
- Zephyr `shell` subsystem documentation.
- Zephyr `flash`/`storage` overview (we won't write to flash in this
  lab, but the concepts apply).
- `docs/design/05-reference-platforms.md` (relevant section).

## Specification

You will build `runtime/zephyr/`, a Zephyr application that:

1. Boots on `qemu_riscv32`.
2. Reserves a static, RX-mapped buffer (using Zephyr's
   `K_APP_DMEM`/MPU regions) of `BPFV_TEXT_MAX_BYTES` (suggest 16 KiB).
3. Exposes a Zephyr shell command:

   ```
   bpfvi load <elf-stream>
   bpfvi run  <prog_id> [hex-ctx]
   bpfvi info
   ```

4. Accepts ELFs over UART, base64-framed (lab provides the framer).
5. Validates (Lab 05) and verifies (Lab 04) before allowing run.
6. Executes via pass-through (Lab 06 logic), respecting the MPU's
   inability to be both writable and executable at the same time.

### Constraints

- **No `malloc`** beyond Zephyr's static heap, which you size at
  compile time.
- **No file system**. ELF lives in RAM, transient.
- **No SMP**. The board is single-core.
- The verifier's step cap is *tighter* than on Linux (call it
  `BPFV_VERIFY_STEPS_EMBEDDED = 8192`).

### MPU and W^X

Zephyr's MPU exposes a fixed number of regions. The lab's CMake
configuration places the BPF-V text region in its own MPU slot. When
loading:

1. Make the region RW from kernel context.
2. Copy and relocate.
3. Make the region RX.
4. Issue `fence.i`.

Use `arch_buffer_validate` to ensure ELF buffers are accessible.

## Tasks

### Task 1 — Skeleton app

`west build -p auto -b qemu_riscv32 runtime/zephyr/` should produce an
image that runs and shows the Zephyr shell. Confirm with `west build
-t run`.

### Task 2 — Port the verifier

The verifier was C99-compatible with no OS dependencies. Replace
`stdio` with Zephyr `LOG_*`, `malloc` with the static arena, and add
the tighter step cap.

### Task 3 — Port the JIT (pass-through)

The same `bpfvi-jit` core from Lab 06 runs here, but allocation is
*static*. The MPU dance replaces `mprotect`.

### Task 4 — Shell commands

Implement `bpfvi load`, `bpfvi run`, `bpfvi info`. Provide a Python
host script `tools/upload.py` to push ELFs over the QEMU serial
console; the lab ships the framer.

### Task 5 — Sample programs

Build, with `riscv32-zephyr-elf-gcc -march=rv32imc -mabi=ilp32`:

- `samples/blinky.bpfv.S` — toggles a Zephyr GPIO via a helper that
  calls `gpio_pin_toggle_dt(...)`.
- `samples/timer.bpfv.S` — uses a helper that reads `k_uptime_get()`
  and returns elapsed ms between two calls.
- `samples/classify.bpfv.S` — takes a 14-byte Ethernet header in
  `ctx`, returns the EtherType.

### Task 6 — Boot-time bench

Time, with `k_cycle_get_32()`:
- ELF receive over UART
- Validate
- Verify
- Pass-through "JIT" (copy + reloc + fence)
- First invocation

Report in `WRITEUP.md`.

## Deliverables

- `runtime/zephyr/` Zephyr application source.
- `tools/upload.py` host helper.
- A passing autograder log on `qemu_riscv32`.
- `WRITEUP.md`:
  - Two things you had to change from your Lab 06 source to fit
    Zephyr. Be specific.
  - How does the MPU restriction differ from Linux's `mmap`/`mprotect`?
  - Why is `fence.i` still required here even on a single-core
    in-order CPU?

## Rubric

| Criterion | Points |
| --------- | ------ |
| Zephyr app boots on `qemu_riscv32` with shell available | 10 |
| Verifier ported and accepts/rejects per Lab 04 tests | 25 |
| Pass-through JIT works in static-allocation regime | 20 |
| All three sample programs run and behave correctly | 25 |
| UART loader and shell commands work | 10 |
| Writeup quality and AI attribution | 10 |
| **Total** | **100** |

## Common pitfalls

- Static buffer not aligned to MPU region granularity → MPU fault on
  exec.
- Forgetting that Zephyr `printf` formatting is `printk`-style.
- Treating `k_malloc` as `malloc`. It can return NULL and you must
  handle it.

## What's next

Lab 10 takes the Zephyr runtime to real hardware: ESP32-C3 or MPFS
Icicle Kit. The runtime should be unchanged; only the board config
moves.
