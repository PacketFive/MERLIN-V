# 12 — CO-RE-V relocation spec

*Status: draft. Pins the layout of CO-RE-V relocations and the
`.merlin.btf` section that backs them. Closes the corresponding
open items in [`04-toolchain.md`](04-toolchain.md) §6 and
extends the reloc records pinned in
[`02-isa-and-bytecode.md`](02-isa-and-bytecode.md) §8.6.*

## 1. What CO-RE-V is

**CO-RE-V** — Compile Once, Run Everywhere on MERLIN-V — is the
mechanism by which a MERLIN-V program written once compiles to a
single object that runs against any kernel/firmware whose data
structures vary in field offsets, sizes, signedness, or
presence. The compiler emits relocation records describing each
access symbolically (type + field name + access kind); the loader
resolves them against the target's BTF at install time and
patches the instruction immediate that encodes the offset (or
size, or shift, ...).

The mechanism mirrors eBPF CO-RE conceptually. The differences
are:

- **Bytecode is RISC-V**, so patched immediates live in real
  RISC-V instruction encodings (`I-type`, `S-type`, `lui`+`addi`
  pairs), not eBPF instruction-word fields.
- **Type information lives in `.merlin.btf`**, not in
  `.BTF` — see §3.
- **The `r_extra1` field of `struct merlin_reloc_v1`** is used,
  not MBZ: it carries the access-string index.

The rest of this document specifies the encoding precisely.

## 2. Programs see CO-RE-V through `<merlin/core.h>`

The source-level interface is the eight intrinsics declared in
[`uapi/merlin/core.h`](uapi/merlin/core.h):

| Intrinsic | Access kind |
| --------- | ----------- |
| `MERLIN_CORE_READ(dst, src)`              | `FIELD_BYTE_OFFSET` (composed with width) |
| `MERLIN_CORE_FIELD_OFFSET(field)`         | `FIELD_BYTE_OFFSET` |
| `MERLIN_CORE_FIELD_EXISTS(field)`         | `FIELD_EXISTS`      |
| `MERLIN_CORE_TYPE_EXISTS(type)`           | `TYPE_EXISTS`       |
| `MERLIN_CORE_ENUM_VALUE(enum, name)`      | `ENUMVAL_VALUE`     |

Each intrinsic emits one of three things into the object:

1. an instruction whose immediate is the *local* field offset
   (the offset as the compiler sees it against the program's
   own BTF);
2. a `struct merlin_reloc_v1` record in `.merlin.relocs` with
   `r_type ∈ {R_MERLIN_CORE_FIELD, R_MERLIN_CORE_SIZE,
   R_MERLIN_CORE_ENUMVAL}`; and
3. an entry in the **access-string table** (§4) whose contents
   describe the field-access chain symbolically.

At install time the loader walks `.merlin.relocs`, resolves each
record against `.merlin.btf` *and* the target's live BTF, and
overwrites the instruction immediate with the target-relative
value. The program's compiled instructions are otherwise
untouched.

## 3. `.merlin.btf` layout

**Decision (pinned).** `.merlin.btf` reuses the upstream BTF wire
format (`linux/btf.h`) unchanged for type definitions. MERLIN-V
adds **no new BTF kinds** and **no MERLIN-specific BTF tags** in
RFC v1.

Rationale:

- BTF is mature, well-tooled, and exactly fits the needs of
  CO-RE-V type-id and field-resolution lookups.
- Reusing BTF means `pahole`, `bpftool`, and existing libbpf
  CO-RE matchers work against MERLIN-V objects with no
  modifications to the type-data path.
- The MERLIN-V-specific bits — access-string encoding, reloc
  records — live in `.merlin.relocs` and `.merlin.btf_ext`
  (§4) outside the BTF type table.

The section's contents are:

```
+----------------------------------------+
| struct btf_header (from linux/btf.h)   |
+----------------------------------------+
| type table (sequence of btf_type ...)  |
+----------------------------------------+
| string table                           |
+----------------------------------------+
```

This is bit-identical to the eBPF `.BTF` section layout.

A program's BTF describes:

- every `ctx` type the program touches (`struct mvdp_md`,
  `struct xdp_md_v`, ...);
- every kernel/firmware type referenced by a CO-RE-V intrinsic;
- every map key/value type referenced by `MERLIN_MAP_DEF`;
- every helper signature (already exposed via the kernel's
  in-kernel BTF, but the program-side copy gives the verifier a
  cross-check).

The loader checks that every `R_MERLIN_CORE_*` record's stem
type-id is a valid type in `.merlin.btf`, and that the resolved
target field has compatible kind (struct vs. union, signedness,
width within the kernel's allowance).

## 4. `.merlin.btf_ext` — access-string table

`.merlin.btf_ext` is the MERLIN-V analogue of eBPF's
`.BTF.ext` — a side table keyed by `.merlin.btf` and consulted
during CO-RE-V resolution. It contains one section:

### 4.1 Section layout

```
+----------------------------------------+
| struct merlin_btf_ext_header           |
+----------------------------------------+
| core_relo_info (per-relo records)      |
+----------------------------------------+
| access_strings (NUL-separated bytes)   |
+----------------------------------------+
```

```c
struct merlin_btf_ext_header {
    __u16 magic;        /* MERLIN_BTF_EXT_MAGIC                   */
    __u8  version;      /* 1                                      */
    __u8  flags;        /* MBZ                                    */
    __u32 hdr_len;      /* sizeof(this header) for forward compat */

    __u32 core_relo_off;
    __u32 core_relo_len;

    __u32 strings_off;
    __u32 strings_len;
};

#define MERLIN_BTF_EXT_MAGIC  0xEB9F      /* matches eBPF .BTF.ext for tool reuse */
```

### 4.2 `core_relo_info` records

```c
struct merlin_core_relo_v1 {
    __u32 insn_off;     /* byte offset into .text where the patch lands */
    __u32 type_id;      /* BTF type-id of the access stem (root type)   */
    __u32 access_str_off; /* offset into the access_strings table       */
    __u32 access_kind;  /* enum merlin_core_access_kind                 */
    __u32 _reserved;    /* MBZ                                          */
};
```

20 bytes per record. The `insn_off` MUST equal the `r_offset` of
the corresponding `R_MERLIN_CORE_*` record in `.merlin.relocs`;
the two records are paired by this offset.

This duality (one record in `.merlin.relocs`, one in
`.merlin.btf_ext`) keeps the binary layout compact: the
`.merlin.relocs` record is fixed-width 24 bytes and tells the
loader "patch this instruction", while the `.merlin.btf_ext`
record carries the variable-length symbolic information the
loader needs to compute the new immediate. Splitting them lets
`merlin-objtool` validate `.merlin.relocs` without parsing BTF.

### 4.3 Access-string encoding

An access string describes a field chain through BTF types in
the format

```
"<stem_field_index>:<member1_index>:<member2_index>:..."
```

— byte-for-byte identical to libbpf's CO-RE access strings (see
`bpf-next/tools/lib/bpf/relo_core.c`). The leading
`stem_field_index` is always `0` for chains rooted at the stem
type itself; it becomes non-zero when accessing array elements
(libbpf precedent).

Example: for source code

```c
__u32 x;
MERLIN_CORE_READ(x, ctx->vlan_tci);
```

with `ctx` typed `struct mvdp_md *`, the compiler emits:

- `r_type = R_MERLIN_CORE_FIELD`
- `r_offset = <addr of the lw instruction>`
- access string `"0:9"` (field 9 of struct mvdp_md is `vlan_tci`
  in the canonical layout)
- access kind `MERLIN_CORE_FIELD_BYTE_OFFSET`

At install time the loader:

1. looks up `mvdp_md` in `.merlin.btf` → resolves to type-id N
2. looks up `mvdp_md` in target's in-kernel BTF → resolves to type-id N'
3. walks `"0:9"` against N → gets field `vlan_tci` with local offset `O`
4. matches `vlan_tci` against N' → gets target offset `O'` (may differ)
5. patches the `lw`'s 12-bit immediate from `O` to `O'`

Reusing libbpf's access-string format is the explicit decision —
tooling (`bpftool btf dump`, `bpftool prog dump core-relos`) that
already understands libbpf access strings works against MERLIN-V
objects with no modification.

## 5. `R_MERLIN_CORE_*` reloc-record additions

Extends the table pinned in
[`02-isa-and-bytecode.md`](02-isa-and-bytecode.md) §8.6.1.

| Field      | For `R_MERLIN_CORE_FIELD` / `_SIZE` / `_ENUMVAL` |
| ---------- | ------------------------------------------------ |
| `r_offset` | byte offset into `.text` where the immediate is patched |
| `r_sym`    | MBZ (the stem type is given by the paired `.merlin.btf_ext` record) |
| `r_addend` | for `_FIELD` kinds that produce a value (`BYTE_OFFSET`, `BYTE_SIZE`, `LSHIFT_U64`, `RSHIFT_U64`): MBZ at emit time; **the loader overwrites it in place with the resolved value** as a debugging aid (and so dumping a relocated `.merlin.relocs` is self-describing). For the boolean kinds (`EXISTS`, `SIGNED`) the resolved value is 0 or 1. |
| `r_extra0` | the **access kind** (`enum merlin_core_access_kind`) |
| `r_extra1` | byte offset into `.merlin.btf_ext`'s `core_relo_info` table identifying the paired record (so the loader can find the access string without scanning). MUST be a multiple of 20. |

Loaders that find a paired `.merlin.btf_ext` record whose
`insn_off` does not match the parent reloc's `r_offset` reject
the program with `-EINVAL`.

## 6. The GCC objtool marker (the `core.h` GCC path)

**Decision (pinned).** Clang provides
`__builtin_preserve_access_index` and friends that lower directly
to LLVM intrinsics; the LLVM backend emits BTF-encoded CO-RE
records. GCC has no such built-in. The MERLIN-V GCC path uses an
*objtool marker* instead:

For each CO-RE-V intrinsic, the GCC header emits:

1. The natural access expression (so the compiler emits a normal
   `lw`/`ld`/`addi` against the local offset).
2. A `__attribute__((section(".merlin.core_v_pending")))`
   placeholder symbol whose name encodes the access chain as a
   character string and whose address is taken just before the
   access instruction.

Example expansion (GCC) of `MERLIN_CORE_READ(x, ctx->vlan_tci)`:

```c
do {
    static const char __merlin_core_marker__1
        __attribute__((section(".merlin.core_v_pending"), used)) =
        "F:mvdp_md:0:9:BYTE_OFFSET";
    __asm__ __volatile__("" :: "r"(&__merlin_core_marker__1));
    __builtin_memcpy(&(x), &(ctx->vlan_tci), sizeof(x));
} while (0)
```

The marker symbol is:

- `F:` for FIELD-kind, `S:` for SIZE, `E:` for ENUMVAL
- `<stem_type_name>:` ASCII type name (resolved via BTF)
- `<access_chain>` colon-separated, libbpf-format
- `:<kind>` access kind name

`merlin-objtool` (`proto-objtool` todo) post-processes the object
file:

1. parses each marker record from `.merlin.core_v_pending`;
2. for each marker, scans the surrounding `.text` for the
   nearest patchable instruction (the address taken in the
   preceding `__asm__`'s register operand is the locator);
3. emits the corresponding `R_MERLIN_CORE_*` record in
   `.merlin.relocs` and the paired record in `.merlin.btf_ext`;
4. interns the access string in the strings table;
5. deletes the `.merlin.core_v_pending` section from the
   output object.

The result is a `.merlin.relocs` + `.merlin.btf_ext` pair
identical to what the Clang path produces directly.

This means the in-kernel and on-target tooling never sees
`.merlin.core_v_pending`; it is a build-time-only transient
section.

## 7. Verifier obligations

The verifier (running on the host after objtool has finished but
before pass-through JIT) checks:

- Every `R_MERLIN_CORE_*` record has a matching
  `.merlin.btf_ext` record at the offset given by `r_extra1`.
- The access-string parses as a sequence of small integers.
- Every type referenced by `core_relo_v1.type_id` exists in
  `.merlin.btf`.
- The instruction at `r_offset` has the encoding the reloc's
  `r_type` requires (e.g., `R_MERLIN_CORE_FIELD` must point at
  an instruction with a 12-bit I-type immediate).
- After loader resolution, the patched immediate is within the
  legal range for the instruction kind.

A program that fails any of these is rejected at `MERLIN_PROG_LOAD`.

## 8. Tooling

- **`merlin-objtool dump core-relos <obj>`** — list every
  `R_MERLIN_CORE_*` record with its paired access string,
  type-id, and access kind. Output format matches
  `bpftool prog dump core-relos` byte-for-byte where possible
  to let existing dashboards re-use the parser.
- **`merlin-objtool patch <obj> --target-btf <kernel.btf>`** —
  resolve all CO-RE-V relocations against a target BTF and emit
  a patched object whose `.text` reflects the resolved
  immediates (useful for offline testing and for embedded
  targets where the loader cannot consult BTF at runtime).
- **`pahole`** — `.merlin.btf` is upstream BTF; pahole emits and
  consumes it unchanged.

## 9. What this document does *not* commit to

- **Bitfield handling beyond `LSHIFT_U64`/`RSHIFT_U64`.** Same
  approach as libbpf; we will copy any future bitfield refinement.
- **CO-RE relocations against types only present at runtime**
  (e.g. modules). Solvable by querying the running kernel's
  BTF at install time; the design here supports it but the
  current `merlin-objtool patch` does not.
- **Per-architecture BTF flavours.** The target's BTF is whatever
  the running kernel produces; cross-architecture CO-RE-V is out
  of scope for RFC v1.
- **`.merlin.btf_ext` line-info / func-info.** libbpf's `.BTF.ext`
  carries line and function tables for verifier diagnostics. The
  MERLIN-V analogue will be added when the verifier gains
  source-mapped error messages; reserved as `func_info_off` /
  `line_info_off` fields in `merlin_btf_ext_header` will be
  introduced at that point (additive change permitted by
  `hdr_len`).
- **A C++-style member-access syntax.** Programs use the
  C member-access syntax through the intrinsics; we do not
  introduce alternative spellings.

## 10. Closing the open items

This document closes the following entries in
[`04-toolchain.md`](04-toolchain.md) §6:

- ~~Decide CO-RE-V reloc record encoding~~ → §3 (BTF reuse) +
  §4 (`.merlin.btf_ext`) + §5 (`r_extra1` use).
- ~~GCC plumbing for CO-RE-V~~ → §6 (the
  `.merlin.core_v_pending` marker section + objtool fix-up).
