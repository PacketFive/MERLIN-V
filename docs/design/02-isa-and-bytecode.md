# 02 — MERLIN-V ISA and Bytecode

Status: profile pinned; helper ABI pinned; ELF format pinned
Owner: PacketFive
Last reviewed: ELF format spec landed

This document defines the MERLIN-V *bytecode* — which is to say, the
profile of the RISC-V ISA that a MERLIN-V program is permitted to use, and
the ABI rules layered on top of it.

References:
- *The RISC-V Instruction Set Manual, Volume I: User-Level ISA* and
  *Volume II: Privileged Architecture* (current ratified versions).
- *RISC-V ABIs Specification* (psABI), ilp32 and lp64 variants.
- *RISC-V Profiles* (RVA20, RVA22, RVA23, RVB23, …).
- `net-next/arch/riscv/include/asm/`, `net-next/Documentation/arch/riscv/`.
- `bpf-next/arch/riscv/net/bpf_jit_*.[ch]` (eBPF-on-RV reference).

## 1. Profile names (decided)

A MERLIN-V program declares which *profile* it requires. Two profiles
are defined; a program names one of them in its `.merlin.meta`
section (§8), and the loader rejects a program whose declared
profile is not enabled on the running execution engine.

| Profile name | march string | psABI | Where it runs |
| ------------ | ------------ | ----- | ------------- |
| **`merlin-linux-rv64`**  | `rv64imac_zicsr_zifencei` | lp64  | Default Linux in-kernel; MPFS Icicle Kit Linux on U54; RV64 SmartNIC firmware. |
| **`merlin-rtos-rv32`**   | `rv32imc_zicsr_zifencei` (optional `_a` for atomics) | ilp32 | Zephyr/RTOS; ESP32-C3-DevKitM-1; MPFS E51 monitor core; FPGA soft cores on PolarFire fabric. |

`zicsr` and `zifencei` appear in both `march` strings because the
*loader* uses CSRs and `fence.i` during install; the program
itself uses neither — see §4.

Future profiles (sleepable, largemem, offload/nic-firmware,
user-mode/sandbox) extend the relaxation set without changing the
base march strings; see [06-verifier.md](06-verifier.md) §7.

### 1.1 Why not adopt RVA20U64 / RVA22U64?

The RISC-V Application Profiles RVA20U64 and RVA22U64 **mandate
F and D** (single- and double-precision floating point). MERLIN-V
forbids FP in kernel paths. Adopting an RVA profile name while
disabling its mandatory extensions would be a misuse of the name.
The project therefore defines its own profile names and pins
their `march` strings explicitly, rather than inheriting and
violating an external profile.

## 2. Permitted base ISAs

The base ISA is `RV32I` (for `merlin-rtos-rv32`) or `RV64I` (for
`merlin-linux-rv64`). No mixing.

## 3. Extension policy (decided per profile)

The per-profile extension policy is pinned. Each row is one of
**M** (mandatory), **O** (optional, profile-controlled flag), or
**F** (forbidden). Where a profile says "M" the loader rejects an
image whose declared `march` does not include the extension;
where a profile says "F" the verifier rejects any instruction
belonging to it.

| Ext | `merlin-linux-rv64` | `merlin-rtos-rv32` | Rationale |
| --- | ----------------- | ---------------- | --------- |
| I (base)            | M | M | Required. |
| M (mul/div)         | M | M | Modern compilers assume it; div-by-zero handled by verifier. |
| A (atomics)         | M | O | Linux side has it everywhere we deploy; some embedded RV32 cores omit it. |
| C (compressed)      | M | M | Code density. Both ESP32-C3 (`rv32imc`) and MPFS support C. |
| F (float)           | F | F | No FP in kernel paths. |
| D (double)          | F | F | As F. |
| Zicsr               | F (programs)<br/>M (runtime) | F (programs)<br/>M (runtime) | Programs cannot read or write CSRs. Loader uses them. |
| Zifencei            | F (programs)<br/>M (runtime) | F (programs)<br/>M (runtime) | Programs do not emit `fence.i`. Loader emits one after copying text. |
| Zba, Zbb, Zbs       | O | O | Bit-manipulation, pure ALU, easy to verify; permitted if hardware provides. |
| Zicboz, Zicbom, Zicbop | F | F | Cache-block management belongs to the kernel, not MERLIN-V programs. |
| Ztso                | O | O | If hardware advertises Ztso, programs may rely on TSO; otherwise RVWMO with explicit fences. |
| V (vector)          | F (Phase 2) | F | Verifier complexity. Revisit post-RFC v1. |
| H (hypervisor), S (supervisor) | F | F | Privileged modes are never available to programs. |

The set of *forbidden* opcodes is a property of the verifier (see
[06-verifier.md](06-verifier.md)). Most of "forbid extension X"
reduces to "reject any instruction whose major+minor opcode
belongs to X."

## 4. Forbidden / restricted instructions (even within enabled extensions)

Independent of extension selection, the following are always rejected:

- All privileged instructions: `mret`, `sret`, `wfi`, `sfence.vma`,
  `hfence.*`, ...
- All CSR instructions (`csrr*`, `csrw*`).
- `ecall` *except* with `a7` set to a registered MERLIN-V helper number
  (see §6 ABI). The verifier must prove `a7` is a compile-time constant
  helper id at every `ecall` site.
- `ebreak` — replaced by a verifier-emitted trap helper if the program
  needs `assert()`-like semantics.
- `fence.i` from program code; only the loader emits it.
- Indirect jumps (`jalr`) targeting anything other than:
  - a verified function pointer derived from a kfunc reloc, or
  - the return address pushed by a verified call sequence.

## 5. Register usage

Standard RISC-V ABI registers carry the following MERLIN-V meaning:

| Reg | psABI role | MERLIN-V role |
| --- | ---------- | ---------- |
| x0 (`zero`) | hardwired zero | same |
| x1 (`ra`) | return address | same; verifier tracks call depth |
| x2 (`sp`) | stack pointer | same; bounded program stack, size declared in meta section |
| x3 (`gp`) | global pointer | reserved by runtime; program does not touch |
| x4 (`tp`) | thread pointer | reserved by runtime |
| x5–x7 (`t0`–`t2`) | temps | general-purpose, caller-saved |
| x8 (`s0`/`fp`) | frame pointer | frame pointer, verifier-tracked |
| x9 (`s1`) | saved | general-purpose, callee-saved |
| x10–x11 (`a0`–`a1`) | args / return | helper args 0–1, helper return in a0 |
| x12–x17 (`a2`–`a7`) | args | helper args 2–7 (a7 doubles as helper id at `ecall`) |
| x18–x27 (`s2`–`s11`) | saved | general-purpose, callee-saved |
| x28–x31 (`t3`–`t6`) | temps | general-purpose, caller-saved |

`a0`–`a5` carry the first 6 helper arguments (matching psABI), `a7`
carries the helper id at the `ecall` site. `a0` carries the helper
return value.

## 6. ABI: program entry, helpers, kfuncs

### 6.1 Program entry

The program's entry is a standard RISC-V function call:

```
ret = prog(ctx)              // a0 = ctx pointer, return in a0
```

`ctx` is a program-type-specific structure (e.g. `struct xdp_md_v`).
The verifier knows the type from MERLIN BTF and tracks every field access.

### 6.2 Helper invocation (source-level encoding)

A program emits the following pattern at every helper-call site:

```
li    a7, HELPER_ID         // a7 = compile-time-constant helper id
mv    a0, ...               // a0..a5 = args 0..5
ecall                       // placeholder; rewritten by loader, see §6.4
// a0 = return value
```

The verifier ensures, at every `ecall` site:

- `a7` is a compile-time constant at the site.
- The helper id is in the program-type-permitted helper set.
- Argument types in `a0`–`a5` match the helper signature.

The `ecall` instruction in this pattern is **not** executed as
`ecall` at runtime. See §6.4 for how the loader transforms it.

### 6.3 kfunc invocation (typed kernel function references)

Kfuncs are dispatched via `jalr` through a reloc'd jump table,
exactly like a normal indirect call:

```
auipc t0, %pcrel_hi(kfunc_tbl)
ld    t0, %pcrel_lo(...)(t0)   // base of table  (lw for rv32)
ld    t0, KFUNC_IDX*8(t0)       // 8/4 for rv64/rv32
jalr  ra, t0, 0
```

A MERLIN-V relocation (`R_MERLIN_KFUNC_SLOT`) marks the table slot
with the kfunc name; the loader resolves it against the running
kernel / firmware kfunc registry.

### 6.4 Loader rewrites at install time

**Decided design.** There is no `ecall` instruction in the
*running* image. The loader rewrites every `ecall` site at
install time, while the executable pages are still RW-mapped, in
both the RISC-V pass-through path and the host-JIT path.

The source-level `li a7, ID; ecall` is exactly 8 bytes (4 bytes
for `li` after constant materialisation, 4 bytes for `ecall`)
when `ID` fits in a 12-bit immediate, which is the only legal
case (helper ids are small). For larger ids the compiler emits
`lui + addi + ecall`; the verifier rejects helper ids that do
not fit the encoding the loader can rewrite.

**RISC-V pass-through path.** The loader replaces the 8-byte
`li a7, ID; ecall` sequence with an 8-byte direct call to the
helper trampoline:

```
auipc t0, %pcrel_hi(trampoline_for_ID)
jalr  ra, %pcrel_lo(...)(t0)
```

Helper trampolines live in a per-program-type table that the
runtime maintains; each trampoline is a few instructions that
set up the helper's typed signature and tail-call into the
helper's C implementation. Because `auipc + jalr` reaches
anywhere in the kernel's 32-bit PC-relative range, the trampoline
table does not have to live next to the program text.

**Host JIT path.** The host JIT translates each verified `ecall`
site into a host-native call into the helper trampoline,
materialising `a7`'s helper id at JIT time and emitting a single
`call rel32` (x86\_64) or `bl` (arm64) into the trampoline. No
indirect dispatch at runtime.

**Why ecall as the marker.** A literal `ecall` would trap to the
next-higher privilege mode on real hardware, which is wrong:
MERLIN-V programs already execute in kernel context in the
pass-through path. The rewrite at install time removes the
problem entirely. Using `ecall` as a marker is convenient
because (a) compilers emit it natively from a `syscall()`-shaped
intrinsic, (b) it is a single 4-byte instruction with a clear
encoding, and (c) any unintended path that fails to rewrite
the site fails loudly with an illegal-instruction-from-kernel
trap rather than silent corruption.

**Verifier obligation.** The verifier must prove the program
contains no executable `ecall` instruction *not* preceded by an
`li a7, CONST` that the verifier could analyse. The loader's
rewrite is then well-defined for every reachable `ecall` site
in the verified image.

## 7. Memory model

Default profile is **RVWMO** (RISC-V Weak Memory Ordering). Programs
that need stricter ordering must:

- Use explicit `fence` instructions, *or*
- Declare a profile with `Ztso` if available on the target.

Atomics use the `A` extension when enabled. The verifier requires that
every shared-memory atomic operate on a properly typed pointer (map
value, ringbuf slot, etc.).

## 8. Object file format

This section is the wire-format specification for MERLIN-V program
objects. A loader that conforms to this section can ingest any
program produced by a conforming toolchain, and vice versa.

### 8.1 Overview

A MERLIN-V object is a standard RISC-V ELF (`EM_RISCV`,
`ELFCLASS32`/`ELFCLASS64` matching the bytecode profile,
`ELFDATA2LSB` always) with these additional named sections:

| Section name      | SHT type      | Purpose                                          |
| ----------------- | ------------- | ------------------------------------------------ |
| `.merlin.meta`      | `SHT_PROGBITS`| Fixed-layout header describing the program       |
| `.merlin.maps`      | `SHT_PROGBITS`| Packed array of map descriptors                  |
| `.merlin.relocs`    | `SHT_PROGBITS`| Packed array of MERLIN-V-specific reloc records     |
| `.merlin.license`   | `SHT_PROGBITS`| NUL-terminated SPDX license identifier           |
| `.merlin.btf`          | `SHT_PROGBITS`| MERLIN BTF type information (see [04-toolchain.md](04-toolchain.md)) |

Plus standard ELF sections used as expected:

- `.text` — the verified RISC-V code.
- `.rodata` — read-only constants. Loader copies into the program's
  R-mapped data region.
- `.symtab`, `.strtab` — standard ELF symbol/string tables. Symbols
  named here are referenced by `.merlin.relocs` records.
- `.rela.text` — standard `SHT_RELA` with `R_RISCV_*` relocs for
  internal PC-relative pairs, `JAL`, etc. The loader processes
  these first, then `.merlin.relocs`.

The following sections, if present, cause the loader to **reject**
the object:

- `.data`, `.bss` — programs have no mutable globals; all state
  lives in maps.
- `.tdata`, `.tbss` — no TLS.
- `.dynamic`, `.interp`, `.got`, `.plt` — programs are
  self-contained; no dynamic linking.

Sections not listed above are ignored.

### 8.2 Endianness and alignment

All multi-byte fields in `.merlin.*` sections are **little-endian**,
regardless of host endianness. This applies even when the object
is generated on a big-endian build host: RISC-V is canonically
little-endian, MERLIN-V images are RISC-V machine code, and the
metadata that describes them follows the same convention.

All structs defined in this section are **naturally aligned**.
All sizes and counts are 32-bit. Strings are fixed-length,
NUL-padded.

### 8.3 `.merlin.meta`

```c
#define MERLIN_META_MAGIC    0x564C524Du   /* 'MRLV' little-endian (M,R,L,V) */
#define MERLIN_META_VERSION  1u

#define MERLIN_NAME_MAX        32
#define MERLIN_TOOLCHAIN_MAX   32

struct merlin_meta_v1 {
    /* --- identity --- */
    uint32_t magic;             /* MERLIN_META_MAGIC */
    uint16_t version_major;     /* 1 for v1; mismatch -> loader rejects */
    uint16_t version_minor;     /* loaders accept any v1.x */
    uint32_t meta_size;         /* sizeof(this struct as emitted); enables forward growth */
    uint32_t flags;             /* MERLIN_META_F_* (see below) */

    /* --- profile and program kind --- */
    uint32_t bytecode_profile;  /* MERLIN_PROFILE_LINUX_RV64 | MERLIN_PROFILE_RTOS_RV32 */
    uint32_t prog_type;         /* MERLIN_PROG_TYPE_*; see 03-kernel-interfaces.md */
    uint32_t expected_attach_type;
    uint32_t reserved0;         /* MBZ; reserved for future verifier_profile_hint */

    /* --- numeric limits the program asks the verifier to apply --- */
    uint32_t requested_stack;   /* bytes; 0 = profile default */
    uint32_t requested_steps;   /* verifier step cap; 0 = profile default */
    uint32_t reserved1[2];      /* MBZ */

    /* --- human-readable identification --- */
    char     prog_name[MERLIN_NAME_MAX];        /* NUL-padded */
    char     toolchain[MERLIN_TOOLCHAIN_MAX];   /* e.g. "gcc 14.2 riscv64-linux-gnu" */
};
```

Total: 80 bytes for v1 (8 + 8 + 16 + 16 + 32 = 80; check: 4+2+2 + 4+4 + 4+4+4+4 + 4+4+8 + 32+32 = 80). MBZ = "must be zero" (loader rejects non-zero values in reserved fields, so the field can be repurposed later without ambiguity).

`flags` bits:

```c
/* Section presence (must match actual section headers) */
#define MERLIN_META_F_HAS_BTF_V    (1u << 0)
#define MERLIN_META_F_HAS_MAPS     (1u << 1)
#define MERLIN_META_F_HAS_RELOCS   (1u << 2)

/* Verifier-profile selection hints (reserved bits 8..15) */
#define MERLIN_META_F_SLEEPABLE    (1u << 8)
#define MERLIN_META_F_LARGEMEM     (1u << 9)

/* Extension-set declarations (reserved bits 16..23) */
#define MERLIN_META_F_NEED_A_EXT   (1u << 16)   /* uses A-ext atomics directly */
#define MERLIN_META_F_NEED_ZBB     (1u << 17)   /* uses Zbb instructions */
#define MERLIN_META_F_NEED_ZBA     (1u << 18)
#define MERLIN_META_F_NEED_ZBS     (1u << 19)
```

Loaders ignore unknown bits but log a warning. Programs MUST NOT
rely on unknown bits being honoured.

#### 8.3.1 Forward-compatibility rules for `merlin_meta_v1`

The `meta_size` field permits growth without breaking older
loaders:

- A loader reads the first 12 bytes of `.merlin.meta` to obtain
  magic + version + meta_size.
- It validates `magic == MERLIN_META_MAGIC` and
  `version_major == 1`.
- It reads `min(meta_size, sizeof(its known struct))` bytes into
  its local copy. Trailing bytes (newer minor versions) are
  ignored.
- A toolchain emitting a newer minor version MUST keep all v1.0
  fields at their original offsets and append new fields.

A future incompatible change requires bumping `version_major`,
at which point the struct becomes `merlin_meta_v2`.

### 8.4 `.merlin.license`

A single NUL-terminated SPDX license identifier string. Examples:

- `"GPL-2.0-only"`
- `"GPL-2.0-or-later"`
- `"GPL-2.0-only WITH Linux-syscall-note"`
- `"Dual GPL-2.0-only OR BSD-2-Clause"`

The loader rejects programs whose license is not GPL-compatible,
matching the policy in `bpf-next/kernel/bpf/syscall.c`'s
`license_is_gpl_compatible()`. The legacy short string `"GPL"`
is accepted for compatibility with existing toolchains but new
objects should use full SPDX identifiers.

### 8.5 `.merlin.maps`

A packed array of fixed-layout records. The number of records is
`section_size / sizeof(struct merlin_map_desc_v1)`; the section
size MUST be a multiple of `sizeof(struct merlin_map_desc_v1)`.

```c
#define MERLIN_MAP_NAME_MAX  32

struct merlin_map_desc_v1 {
    char     name[MERLIN_MAP_NAME_MAX];   /* NUL-padded; loader-visible identity */
    uint32_t type;                       /* MERLIN_MAP_TYPE_* */
    uint32_t key_size;                   /* bytes */
    uint32_t value_size;                 /* bytes */
    uint32_t max_entries;
    uint32_t flags;                      /* MERLIN_MAP_F_* (parallel to BPF's BPF_F_*) */
    uint32_t key_btf_id;                 /* index into .merlin.btf; 0 = untyped */
    uint32_t value_btf_id;               /* index into .merlin.btf; 0 = untyped */
    uint32_t inner_map_idx;              /* for map-in-map; 0xFFFFFFFFu = none */
    uint32_t reserved[3];                /* MBZ */
};
```

Total: 80 bytes per record (32 + 4*9 + 12 = 80).

Map type numbers:

```c
#define MERLIN_MAP_TYPE_UNSPEC          0
#define MERLIN_MAP_TYPE_HASH            1
#define MERLIN_MAP_TYPE_ARRAY           2
#define MERLIN_MAP_TYPE_PERCPU_HASH     3
#define MERLIN_MAP_TYPE_PERCPU_ARRAY    4
#define MERLIN_MAP_TYPE_LRU_HASH        5
#define MERLIN_MAP_TYPE_LPM_TRIE        6
#define MERLIN_MAP_TYPE_RINGBUF         7
#define MERLIN_MAP_TYPE_PROG_ARRAY      8   /* tail calls */
#define MERLIN_MAP_TYPE_XSKMAP          9   /* AF_XDP redirect */
#define MERLIN_MAP_TYPE_MVSKMAP        10   /* AF_MVDP redirect; see
                                               docs/design/08-mvdp-and-af-mvdp.md §4 */
/* 11..127  reserved for future MERLIN-V map types */
/* 128..255 reserved for vendor / offload-specific map types */
```

Where the semantic is identical, these numbers correspond 1:1
with the matching `BPF_MAP_TYPE_*` from `bpf-next/include/uapi/linux/bpf.h`.
The MERLIN-V kernel-side path implements them by delegating to the
existing `struct bpf_map_ops` instances (see
[03-kernel-interfaces.md](03-kernel-interfaces.md) §5).

### 8.6 `.merlin.relocs`

A packed array of fixed-layout records:

```c
struct merlin_reloc_v1 {
    uint32_t r_offset;   /* byte offset into .text where the reloc applies */
    uint32_t r_type;     /* R_MERLIN_* */
    uint32_t r_sym;      /* type-specific: see per-type table */
    int32_t  r_addend;   /* signed addend */
    uint32_t r_extra0;   /* type-specific (e.g. CO-RE access kind) */
    uint32_t r_extra1;   /* type-specific; MBZ if unused */
};
```

24 bytes per record. The number of records is
`section_size / 24`; section size MUST be a multiple of 24.

Records MUST be sorted by `r_offset` ascending and MUST NOT
overlap. `r_offset` MUST point inside `.text`.

#### 8.6.1 `R_MERLIN_*` type numbers

The MERLIN-V reloc number space is **independent of the
`R_RISCV_*` space**. Standard RISC-V relocs (PC-relative pairs,
`JAL`, etc.) live in `.rela.text` with their normal numbering;
MERLIN-V relocs live in `.merlin.relocs` with this numbering:

| Type                     | Value | Patches at `r_offset`                         | Resolves against                                          |
| ------------------------ | ----- | --------------------------------------------- | --------------------------------------------------------- |
| `R_MERLIN_NONE`            | 0     | (no-op)                                       | (none)                                                    |
| `R_MERLIN_HELPER_ID`       | 1     | 12-bit I-imm of `li a7, ?` (or `lui`+`addi` pair for larger ids) | helper id, looked up by `.symtab[r_sym].st_name`        |
| `R_MERLIN_KFUNC_SLOT`      | 2     | 12-bit I-imm of the `ld`/`lw` against the kfunc table | kfunc by name in `.symtab[r_sym]`                  |
| `R_MERLIN_MAP_FD`          | 3     | full 32-bit immediate pair (`lui`+`addi`)     | map descriptor index: `r_sym` is the index into `.merlin.maps` (not `.symtab`) |
| `R_MERLIN_MAP_VALUE`       | 4     | full 32-bit immediate pair                    | map value pointer at offset `r_addend`; `r_sym` is `.merlin.maps` index |
| `R_MERLIN_CORE_FIELD`      | 5     | 12-bit I-imm of a load/store                  | BTF field offset; `r_extra0` = access kind (see below)    |
| `R_MERLIN_CORE_SIZE`       | 6     | 12-bit I-imm of an `addi` (size literal)      | BTF type size                                             |
| `R_MERLIN_CORE_ENUMVAL`    | 7     | 12-bit I-imm                                  | BTF enum value                                            |
| `R_MERLIN_PROG_ENTRY`      | 8     | full 32-bit immediate pair                    | another MERLIN-V program's entry, by `.symtab[r_sym].st_name` (for tail calls) |
| (9..63)                  |       | reserved for future MERLIN-V relocs              |                                                           |
| (64..127)                |       | reserved for vendor / offload-specific relocs |                                                           |

Loaders reject any record whose `r_type` is in the
"reserved for future" range — default-deny.

#### 8.6.2 CO-RE access kinds (for `R_MERLIN_CORE_FIELD`)

`r_extra0` carries the access kind:

```c
#define MERLIN_CORE_FIELD_BYTE_OFFSET  0
#define MERLIN_CORE_FIELD_BYTE_SIZE    1
#define MERLIN_CORE_FIELD_EXISTS       2
#define MERLIN_CORE_FIELD_SIGNED       3
#define MERLIN_CORE_FIELD_LSHIFT_U64   4
#define MERLIN_CORE_FIELD_RSHIFT_U64   5
```

Semantics match libbpf's CO-RE relocation handling; see
`bpf-next/tools/lib/bpf/relo_core.h`. MERLIN-V's loader implements
the same resolution logic, against `.merlin.btf` and the running
kernel's BTF.

### 8.7 `.merlin.btf`

MERLIN BTF type information. The on-disk format is BTF (per
`bpf-next/Documentation/bpf/btf.rst`) with the project-specific
extensions defined in [04-toolchain.md](04-toolchain.md) §3.
Endianness and alignment rules in §8.2 apply. This section is
referenced by `key_btf_id` and `value_btf_id` in
`.merlin.maps` records and by `r_sym` in CO-RE relocs.

### 8.8 Sample loader pseudocode

```c
int merlin_load_object(const void *elf, size_t elf_size,
                     struct merlin_prog **out)
{
    const Elf_Ehdr *eh = elf;

    if (eh->e_ident[EI_DATA]  != ELFDATA2LSB)   return -EINVAL;
    if (eh->e_machine          != EM_RISCV)     return -EINVAL;

    struct merlin_meta_v1 *meta_raw =
        find_section(elf, ".merlin.meta");
    if (!meta_raw)                              return -ENOENT;

    /* Read the smallest portion that contains magic+version+meta_size */
    if (meta_raw->magic         != MERLIN_META_MAGIC) return -EINVAL;
    if (meta_raw->version_major != 1)               return -EINVAL;

    struct merlin_meta_v1 meta = {0};
    size_t copy = MIN(meta_raw->meta_size,
                      sizeof(struct merlin_meta_v1));
    memcpy(&meta, meta_raw, copy);

    /* Validate ELFCLASS matches the declared bytecode profile */
    if (meta.bytecode_profile == MERLIN_PROFILE_LINUX_RV64
        && eh->e_ident[EI_CLASS] != ELFCLASS64) return -EINVAL;
    if (meta.bytecode_profile == MERLIN_PROFILE_RTOS_RV32
        && eh->e_ident[EI_CLASS] != ELFCLASS32) return -EINVAL;

    /* License check */
    const char *lic = find_section(elf, ".merlin.license");
    if (!lic || !license_is_gpl_compatible(lic)) return -EPERM;

    /* Reject forbidden sections */
    if (has_section(elf, ".data")  || has_section(elf, ".bss")
     || has_section(elf, ".tdata") || has_section(elf, ".tbss")
     || has_section(elf, ".dynamic"))
        return -EINVAL;

    /* Parse .merlin.maps; create kernel maps */
    /* Verify .text under the selected verifier profile */
    /* Apply .rela.text (standard R_RISCV_*) */
    /* Apply .merlin.relocs (R_MERLIN_*) */
    /* Allocate exec memory, copy text, fence.i, install */

    return 0;
}
```

### 8.9 What this spec deliberately excludes

| Excluded                                        | Why                                                |
| ----------------------------------------------- | -------------------------------------------------- |
| Mutable globals (`.data`, `.bss`)               | All state lives in maps; programs are reentrant.   |
| Thread-local storage                            | Programs are not threads.                          |
| Dynamic linking (`DT_NEEDED`, `dlopen`, PLT)    | Programs are self-contained; load-time resolution. |
| Floating-point relocs                           | F/D extensions forbidden in both profiles (§3).    |
| C++ exceptions, `.eh_frame` runtime use         | No exceptions in MERLIN-V programs.                   |
| Compressed ELF sections (`SHF_COMPRESSED`)      | Loader stays simple; compress upstream if needed.  |
| Multiple `.text` sections                       | Single entry; programs are a single compilation unit. |

Each exclusion is a deliberate decision and may be revisited in
a future major version. Until then, a loader that encounters one
of these MUST reject the object.

### 8.10 Open items

- `R_MERLIN_*` numbers above 8 are reserved but unassigned. As new
  reloc kinds are needed (e.g. for sleepable program entries,
  for SIMD support if `V` lands), they are assigned here in
  ascending order.
- Vendor / offload-specific map types (128..255) and reloc types
  (64..127): vendor registration mechanism. Out of scope for
  RFC v1; document in a follow-up if any vendor approaches.
- The exact MERLIN BTF extension format (referenced by §8.7) is
  specified in [04-toolchain.md](04-toolchain.md) §3 and is its
  own open item.

## 9. Worked micro-example

A minimal "drop everything" XDP-V program (conceptual, RV64I):

```c
#include <merlin/xdp.h>

SEC("xdp")
int drop_all(struct xdp_md_v *ctx)
{
    return XDP_V_DROP;
}
```

Compiles (with stock `riscv64-unknown-linux-gnu-gcc -O2
-ffreestanding -mcmodel=medany -mabi=lp64
-march=rv64imac_zicsr_zifencei -nostdlib`) to roughly:

```asm
drop_all:
    li      a0, 1          # XDP_V_DROP
    ret
```

— two instructions, no helpers, no relocations. On a RISC-V host this
loads at native speed; on x86\_64 the host JIT emits two x86
instructions. There is no eBPF-style decode step.

## 10. Open items

- Decide whether `A`-extension AMOs are permitted directly in
  programs or only via helpers (independent of profile membership;
  `A` is mandatory on `merlin-linux-rv64` but the verifier policy
  for raw AMOs is a separate question).
- Decide whether vector (`V`) becomes a per-program flag inside
  `merlin-linux-rv64` post-RFC, or a new profile name
  (`merlin-linux-rv64v`). Out of scope for RFC v1.
- Decide policy on linker relaxation: verify before or after
  relaxation, or require `-mno-relax` from the compiler. See
  also [06-verifier.md](06-verifier.md) §8.
- Vendor / offload-specific map types (128..255) and reloc
  types (64..127) — vendor registration mechanism. Out of scope
  for RFC v1.

The following previously-open items are now **decided** (recorded
here for traceability):

- ~~Pin a specific RISC-V profile~~ → §1, decided: two project
  profiles, `merlin-linux-rv64` and `merlin-rtos-rv32`.
- ~~Decide compressed-instruction policy~~ → §3, decided: `C` is
  mandatory in both profiles.
- ~~Helper invocation: `ecall` with `a7`, or jump-table~~ → §6.2
  and §6.4, decided: `ecall` source-level encoding, loader
  rewrites to direct call at install time.
- ~~Define exact `.merlin.*` section binary layouts~~ → §8.3–§8.7,
  decided.
- ~~Define MERLIN-V-specific ELF reloc type numbers (`R_MERLIN_*`)~~
  → §8.6.1, decided.
