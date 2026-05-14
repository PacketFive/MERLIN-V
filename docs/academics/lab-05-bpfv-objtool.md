# Lab 05 — `bpfv-objtool`

Module: 3
Effort tier: M
Prerequisites: Lab 04.
Design docs: [`02-isa-and-bytecode.md`](../design/02-isa-and-bytecode.md) §8,
[`04-toolchain.md`](../design/04-toolchain.md).

## Learning objectives

- Parse ELF using `libelf`.
- Validate BPF-V-specific sections (`.bpfv.meta`, `.bpfv.maps`,
  `.bpfv.relocs`, `.BTF.v`).
- Catalogue relocations and resolve them, on paper, against a mock
  kernel symbol table.
- Build a CLI tool that mirrors part of `bpftool`'s object-inspection
  surface.

## Specification

You will build `bpfv-objtool`, with these subcommands:

```
bpfv-objtool prog dump <file>
bpfv-objtool prog xlated <file>
bpfv-objtool prog validate <file>
bpfv-objtool elf info <file>
bpfv-objtool reloc list <file>
```

### `.bpfv.meta` (binary layout — pin to this exactly)

```c
struct bpfv_meta_v1 {
    uint32_t magic;           // 'B','P','F','V' == 0x56465042
    uint32_t version;         // 1
    uint32_t profile;         // BPFV_PROFILE_*
    uint32_t prog_type;       // BPFV_PROG_TYPE_*
    uint32_t expected_attach_type;
    uint32_t btf_offset;      // file offset of .BTF.v
    uint32_t btf_size;
    uint32_t maps_offset;
    uint32_t maps_size;
    uint32_t relocs_offset;
    uint32_t relocs_size;
    char     name[32];        // NUL-terminated
    char     license[64];     // NUL-terminated
};
```

### `.bpfv.maps`

A packed array of:

```c
struct bpfv_map_desc_v1 {
    char     name[32];
    uint32_t type;       // BPFV_MAP_TYPE_*
    uint32_t key_size;
    uint32_t value_size;
    uint32_t max_entries;
    uint32_t flags;
};
```

### `.bpfv.relocs`

A packed array of:

```c
struct bpfv_reloc_v1 {
    uint32_t r_offset;   // offset into .text
    uint32_t r_type;     // R_BPFV_*
    uint32_t r_sym;      // index into a symbol table (see below)
    int32_t  r_addend;
};
```

Symbol table is the lab's own little thing: a packed string table
inside `.bpfv.symtab` plus a parallel array of `(string_offset,
target_section)` records. We deliberately use this rather than
inflating the ELF symbol table so students focus on the BPF-V format
without ELF-namespace battles.

`R_BPFV_*` types this lab cares about:

| Name | Meaning |
| ---- | ------- |
| `R_BPFV_HELPER_ID` | Patch the 12-bit immediate of `li a7, ?` |
| `R_BPFV_KFUNC_SLOT` | Patch a `ld` immediate addressing a kfunc table slot |
| `R_BPFV_MAP_FD` | Patch a 32-bit immediate sequence with a map identity |
| `R_BPFV_CORE_FIELD` | Patch a 12-bit immediate with a CO-RE-V offset |

### Required validations (`prog validate`)

1. ELF is `EM_RISCV`, class matches the profile (32 / 64).
2. `.bpfv.meta`'s `magic` and `version` are correct.
3. Every map referenced by a reloc exists in `.bpfv.maps`.
4. Every helper id referenced by a reloc is in the program type's
   helper set (load the program-type record from
   `tools/bpfv-objtool/data/prog_types.json`, shipped in skeleton).
5. The license is GPL-compatible.
6. The relocation list is sorted by `r_offset` and has no overlaps.
7. No reloc points outside `.text`.

If any check fails, exit non-zero with a structured error.

## Tasks

### Task 1 — `elf info`

Print ELF class, endianness, machine, entry, sections, segments. Use
`libelf`. The output is graded by comparing to a golden file with
some normalisation (lab provides the diff script).

### Task 2 — `prog dump`

Print `.bpfv.meta`, `.bpfv.maps` in human-readable form. Dump
`.bpfv.relocs` as a table.

### Task 3 — `prog xlated`

Print the disassembled `.text` instructions with reloc annotations
inline:

```
0x00000010   13 05 a0 00   li   a0, 10
0x00000014   93 08 b0 03   li   a7, 59       ; R_BPFV_HELPER_ID:trace_log
0x00000018   73 00 00 00   ecall
```

You may shell out to `riscv64-linux-gnu-objdump -Mnumeric -d` or
`llvm-objdump --triple=riscv64 -d` for the disassembly proper.

### Task 4 — `prog validate`

Implement the validations above. The lab ships `tests/objs/` with
both well-formed and deliberately broken inputs; each broken input is
labelled with the check it should fail and your tool must report the
matching error code.

### Task 5 — `reloc list`

A focused subcommand that prints just the reloc table, one per line,
plus the symbolic name resolved against `.bpfv.symtab`.

## Deliverables

- `tools/bpfv-objtool/` source.
- A passing autograder log.
- `WRITEUP.md` covering:
  - Why is the BPF-V symbol table *separate* from the ELF symbol
    table in this lab's design?
  - One real-world ELF gotcha you ran into (e.g., section header
    string table missing, `sh_link` wrong, big-endian surprise).
  - How would `R_BPFV_CORE_FIELD` resolution differ in-kernel from in
    this tool?

## Rubric

| Criterion | Points |
| --------- | ------ |
| `elf info` matches the golden output | 15 |
| `prog dump` matches the golden output | 15 |
| `prog xlated` produces correct annotations | 20 |
| `prog validate` catches every break in `tests/objs/` | 30 |
| `reloc list` correct | 10 |
| Writeup quality and AI attribution | 10 |
| **Total** | **100** |

## Common pitfalls

- Forgetting that `.bpfv.*` are SHT\_PROGBITS with custom flags. The
  ELF spec is fine with this; some readers complain.
- Confusing file offsets with section offsets. `.bpfv.meta`'s
  `btf_offset` is *file* offset.
- Treating `r_offset` as a virtual address instead of an offset into
  `.text`.

## What's next

Lab 06 takes the validated program and actually runs it inside an
`mmap`'d executable region — the user-space "pass-through JIT."
