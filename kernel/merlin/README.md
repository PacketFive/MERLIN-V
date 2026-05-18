# kernel/merlin/ — MERLIN-V in-kernel JIT VM (out-of-tree prototype)

Status: **prototype / RFC staging**
Cross-references: `docs/design/03-kernel-interfaces.md`, `docs/design/06-verifier.md`,
`docs/design/07-jit-and-offload.md`

---

## What is this?

MERLIN-V is an in-kernel JIT virtual machine whose bytecode is a restricted
profile of the RISC-V ISA (`rv64imac_zicsr_zifencei` for Linux programs).
On a RISC-V host, verified programs run pass-through — the bytecode *is* native
machine code. On other hosts (x86\_64, arm64, …) a thin translation JIT is run.

This directory contains the out-of-tree kernel module that prototypes the
`merlin(2)` syscall, the ELF loader, the abstract-interpretation verifier, and
both the RISC-V pass-through and x86\_64 host JITs.

The design record lives in `docs/design/`. This module will be reorganised into
an in-tree patch series against `bpf-next` as `kernel/merlin/` per
`docs/design/03-kernel-interfaces.md §1`.

---

## Source layout

```
kernel/merlin/
├── Kconfig                 # CONFIG_MERLIN, CONFIG_MERLIN_JIT_{X86_64,RISCV}
├── Makefile                # Out-of-tree and in-tree build rules
├── include/
│   └── merlin_internal.h  # All internal types, IDR decls, verifier API
├── core.c                  # IDR, refcounting, anon-inode fd lifecycle
├── decode.c                # RV32/RV64 instruction decoder (kernel C)
├── verifier.c              # Abstract-interpretation verifier
├── loader.c                # ELF parser, section finder, load pipeline
├── syscall.c               # merlin(2) multiplexer + /dev/merlin misc device
├── dispatch.c              # Helper registry + merlin_dispatch_helper()
├── maps.c                  # Thin wrappers over kernel/bpf/ map ops
└── jit/
    ├── select.c            # Pick JIT backend by host ISA
    ├── pass_through.c      # RISC-V pass-through (copy + I-cache flush)
    └── arch/
        └── x86_64.c       # RV64 → x86_64 single-pass spill-everything JIT
```

---

## Build (out-of-tree)

```bash
# Requires a configured kernel source tree.
make -C /path/to/linux M=$(pwd)/kernel/merlin
# or using the default (running kernel headers):
cd kernel/merlin && make
```

The module is `merlin.ko`. Load with:

```bash
insmod merlin.ko
# creates /dev/merlin with mode 0600
```

To use from user space, open `/dev/merlin` and issue `MERLIN_IOC_CMD` ioctls
carrying a `struct merlin_ioc_args { u32 cmd; u32 attr_sz; u64 attr_ptr; }`.
This mirrors the `merlin(2)` syscall interface without requiring a kernel patch.

---

## Key design decisions

### Verifier (verifier.c / decode.c)

Phase 1 implements a linear-pass abstract interpreter (no CFG joins, no
widening, no bounded-loop analysis). The abstract register domain:

| Kind | Meaning |
|------|---------|
| `RVAL_UNKNOWN` | Unconstrained integer |
| `RVAL_CONST(v)` | Fully known 64-bit constant |
| `RVAL_PTR_CTX(off)` | Pointer to program context + offset |
| `RVAL_PTR_STACK(off)` | Pointer to current stack frame + offset |
| `RVAL_PTR_HELPER_RET(id)` | Return value of a known helper |

The verifier:
- Rejects any instruction outside the declared profile.
- Rejects back-edges (loops deferred to Phase 2).
- Rejects any `ecall` where `a7` is not a compile-time constant in the
  program-type's helper allowlist.
- Rejects any `LOAD`/`STORE` whose address register is `UNKNOWN` or `CONST`
  (i.e. not derived from a typed pointer root).

### JIT — x86\_64 (jit/arch/x86\_64.c)

Spill-everything strategy: all 32 RV registers live in a `regs[32]` array
(`u64[32]`, 256 bytes) on the JIT'd function's stack frame; `rbx` holds
`&regs[0]`. Each RV instruction translates to:

```
mov rax, [rbx + rs1*8]
mov rcx, [rbx + rs2*8]
<op> rax, rcx
mov [rbx + rd*8], rax
```

No register allocation; no CFG analysis. Adequate for prototype correctness;
performance optimisation is a Phase 2 concern.

`ecall` translates to a call to `merlin_jit_x86_helper_trampoline(regs)`, which
reads `a7`/`a0-a5` and calls `merlin_dispatch_helper()`.

### JIT — RISC-V pass-through (jit/pass_through.c)

Copies the verified bytecode to a `vmalloc_exec` region, flushes the I-cache
(`flush_icache_range`), and returns the function pointer. No instruction
translation at all.

### Maps (maps.c)

MERLIN-V reuses `kernel/bpf/` map storage. `MERLIN_MAP_*` commands translate
the `merlin_map_type` enum to `BPF_MAP_TYPE_*` and delegate to `__sys_bpf`.
MERLIN-V-specific map types (e.g. `MVSKMAP` for AF\_MVDP socket redirect) are
`EOPNOTSUPP` until implemented natively.

---

## Out-of-scope (prototype)

- `MERLIN_LINK_*` (program attachment to hooks): stubbed, returns `EOPNOTSUPP`.
- MVCP control-plane commands (`MERLIN_MAP_BATCH_TXN_*`, `MERLIN_NS_*`,
  `MERLIN_KEYRING_BIND`): stubbed. The user-space prototypes are in
  `tools/merlin-txn/`, `tools/merlin-ns/`, `tools/merlin-sign/`.
- `MERLIN_BTF_*`: stubbed.
- C-extension (16-bit) instructions: decoder marks them `INSN_UNSUPPORTED`;
  verifier rejects them. Full C-ext support is Phase 2.
- Bounded loops (`merlin_loop()` helper): Phase 2.

---

## Assisted-by

Copilot-CLI:Claude-Opus
