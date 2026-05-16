# Lab 05 — `merlin-objtool`

Module: 3
Effort tier: M
Prerequisites: Lab 04.
Design docs: [`02-isa-and-bytecode.md`](../design/02-isa-and-bytecode.md) §8,
[`04-toolchain.md`](../design/04-toolchain.md).

## Learning objectives

- Parse ELF using `libelf`.
- Validate MERLIN-V-specific sections (`.merlin.meta`, `.merlin.maps`,
  `.merlin.relocs`, `.merlin.btf`).
- Catalogue relocations and resolve them, on paper, against a mock
  kernel symbol table.
- Build a CLI tool that mirrors part of `bpftool`'s object-inspection
  surface.

## Specification

You will build `merlin-objtool`, with these subcommands:

```
merlin-objtool prog dump <file>
merlin-objtool prog xlated <file>
merlin-objtool prog validate <file>
merlin-objtool elf info <file>
merlin-objtool reloc list <file>
```

### Binary format — the canonical spec

The on-disk binary format (`.merlin.meta`, `.merlin.license`,
`.merlin.maps`, `.merlin.relocs`, `.merlin.btf`) is specified in
[`docs/design/02-isa-and-bytecode.md`](../design/02-isa-and-bytecode.md)
§8. **That section is authoritative.** This lab implements
exactly what §8 specifies; if §8 changes, this lab is updated to
match (or vice versa, with the lab's author proposing the change
to §8 first).

Key structures the lab handles:

- `struct merlin_meta_v1`   — see §8.3 (80-byte fixed header).
- `struct merlin_map_desc_v1` — see §8.5 (80 bytes per record).
- `struct merlin_reloc_v1` — see §8.6 (24 bytes per record).
- `.merlin.license` — NUL-terminated SPDX string (see §8.4).
- `.merlin.btf` — MERLIN BTF (see [04-toolchain.md](../design/04-toolchain.md) §3).

The lab uses the **ELF symbol table** (`.symtab`/`.strtab`) for
naming, exactly as the spec requires (§8.6). There is no
"lab-private symbol table"; an earlier draft of this lab had one
and that draft was wrong. The autograder and the spec are now
aligned.

### `R_MERLIN_*` types this lab handles

From §8.6.1 of the spec:

| Name                  | Value | Lab behaviour                                              |
| --------------------- | ----- | ---------------------------------------------------------- |
| `R_MERLIN_NONE`         | 0     | accepted; no-op                                            |
| `R_MERLIN_HELPER_ID`    | 1     | resolves a helper id from `.symtab` by name; validates the helper is in the program type's allow list (`tools/merlin-objtool/data/prog_types.json`) |
| `R_MERLIN_KFUNC_SLOT`   | 2     | resolves a kfunc slot index; validates the kfunc name      |
| `R_MERLIN_MAP_FD`       | 3     | validates that `r_sym` is a valid index into `.merlin.maps`  |
| `R_MERLIN_MAP_VALUE`    | 4     | as above, plus checks `r_addend` is within value bounds    |
| `R_MERLIN_CORE_FIELD`   | 5     | validates `r_extra0` is one of the access kinds (§8.6.2)   |
| `R_MERLIN_CORE_SIZE`    | 6     | validates target BTF type                                  |
| `R_MERLIN_CORE_ENUMVAL` | 7     | validates target BTF enum                                  |
| `R_MERLIN_PROG_ENTRY`   | 8     | validates target program name exists in `.symtab`          |

Any other `r_type` value: validate rejects, per the spec's
default-deny rule (§8.6.1).

### Required validations (`prog validate`)

1. ELF is `EM_RISCV`, class matches the profile (32 / 64).
2. `.merlin.meta`'s `magic` and `version` are correct.
3. Every map referenced by a reloc exists in `.merlin.maps`.
4. Every helper id referenced by a reloc is in the program type's
   helper set (load the program-type record from
   `tools/merlin-objtool/data/prog_types.json`, shipped in skeleton).
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

Print `.merlin.meta`, `.merlin.maps` in human-readable form. Dump
`.merlin.relocs` as a table.

### Task 3 — `prog xlated`

Print the disassembled `.text` instructions with reloc annotations
inline:

```
0x00000010   13 05 a0 00   li   a0, 10
0x00000014   93 08 b0 03   li   a7, 59       ; R_MERLIN_HELPER_ID:trace_log
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
plus the symbolic name resolved against the ELF `.symtab` (or the
map name from `.merlin.maps` when `r_type` is `R_MERLIN_MAP_FD` /
`R_MERLIN_MAP_VALUE`).

## Deliverables

- `tools/merlin-objtool/` source.
- A passing autograder log.
- `WRITEUP.md` covering:
  - Why does the MERLIN-V format reuse the **ELF `.symtab`** for
    helper and kfunc naming rather than carrying its own
    `.merlin.symtab`? (Hint: read §8.6 of the design doc — what
    do `r_sym` values point at?)
  - One real-world ELF gotcha you ran into (e.g., section header
    string table missing, `sh_link` wrong, an endianness surprise
    on a big-endian build host).
  - How would `R_MERLIN_CORE_FIELD` resolution differ in-kernel
    from in this tool?

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

- Forgetting that `.merlin.*` sections are `SHT_PROGBITS`
  identified by section name, not by a custom `SHT_*` type. The
  ELF spec is fine with this; some tools complain.
- Forgetting the endianness rule: `.merlin.*` are always little-endian
  even when the build host is big-endian
  (see [design 02 §8.2](../design/02-isa-and-bytecode.md#82-endianness-and-alignment)).
- Treating `r_offset` as a virtual address instead of a byte
  offset into `.text`.
- Reading `merlin_meta_v1` as a fixed-size struct rather than as
  the first `min(meta_size, sizeof your struct)` bytes — the
  forward-compatibility rule in §8.3.1.

## What's next

Lab 06 takes the validated program and actually runs it inside an
`mmap`'d executable region — the user-space "pass-through JIT."
