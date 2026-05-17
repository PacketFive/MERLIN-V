# `merlin-objtool` — prototype

The first user-space code of MERLIN-V.  A small CLI that parses a
MERLIN-V ELF object and validates the layout pinned in
[`../../docs/design/02-isa-and-bytecode.md`](../../docs/design/02-isa-and-bytecode.md)
§8 and the program-side headers in
[`../../docs/design/uapi/merlin/`](../../docs/design/uapi/merlin/).

This is intentionally minimal: a single C source plus a Makefile.
It runs through the canonical fixture
([`docs/design/uapi/examples/drop_all.c`](../../docs/design/uapi/examples/drop_all.c))
end-to-end on the host, exercising the full UAPI surface we have
designed so far.

## Subcommands

```text
merlin-objtool info        <object.o>   - one-line summary
merlin-objtool dump-meta   <object.o>   - print .merlin.meta fields
merlin-objtool dump-maps   <object.o>   - list .merlin.maps records
merlin-objtool dump-relocs <object.o>   - list .merlin.relocs records
merlin-objtool sha256      <object.o>   - hash the signed region
merlin-objtool validate    <object.o>   - run all static checks
```

## Build

```bash
sudo apt install libelf-dev libssl-dev
cd tools/merlin-objtool
make            # builds merlin-objtool
make test       # builds the fixture and runs every subcommand
```

The `make test` target builds the fixture for the host (not RV64)
so the resulting `.o` has `e_machine = EM_X86_64` rather than
`EM_RISCV`.  Every subcommand except `validate` runs successfully
on the host build; `validate` is expected to fail on the
`e_machine` strict check, and the Makefile target accepts that as
a pass.  On a real RV64 cross-compile (see the build command in
the example file's header comment) `validate` returns zero.

## What's validated

- ELF endianness is little-endian (LE-always per §8.2).
- ELF e_machine is `EM_RISCV` (or `EM_NONE`) — strict mode only.
- `.merlin.meta` is present, has the right magic, version, and
  internal sizes.  Forward-compat rule (§8.3.1) is enforced:
  `meta_size` must be ≥ 12 (the minimum prefix that lets a loader
  parse the header), and ≤ section size.
- `.merlin.license` is present and NUL-terminated.
- `.merlin.maps` size is a multiple of `sizeof(merlin_map_desc_v1)`
  (80 bytes); every record has a valid (non-UNSPEC) type and
  reasonable max_entries.
- `.merlin.relocs` size is a multiple of `sizeof(merlin_reloc_v1)`
  (24 bytes); records are sorted by `r_offset`; no reserved
  reloc types appear.
- `.merlin.btf`, `.merlin.btf_ext`, `.merlin.core_v_pending`,
  `.merlin.sig` presence is recorded but not validated
  structurally (those validators are follow-up increments).

## What's not yet implemented

- CO-RE-V marker resolution: parsing `.merlin.core_v_pending` and
  emitting `.merlin.relocs` + `.merlin.btf_ext` records.  Spec:
  [`../../docs/design/12-core-v-spec.md`](../../docs/design/12-core-v-spec.md) §6.
- BTF validation: requires a BTF parser.  Will reuse libbpf's BTF
  reader rather than rolling our own; see `proto-verifier-userland`
  for that integration.
- Signed-region signature verification: depends on
  `proto-mvcp-signed-progs`.
- Cross-resolution of CO-RE-V relocations against a target BTF
  (the `merlin-objtool patch` subcommand).
- Disassembly: leans on `binutils` `objdump -d -Mriscv` per
  [`../../docs/design/04-toolchain.md`](../../docs/design/04-toolchain.md) §5.

## What this prototype demonstrates

End-to-end the headers, the ELF layout, the magic numbers, the
struct sizes, and the canonical section names all agree.  The
SHA-256 over the signed region is deterministic and computable
from any host that can read the ELF — exactly what MVCP
attestation (see
[`../../docs/design/11-mvcp-attestation.md`](../../docs/design/11-mvcp-attestation.md))
will rely on for `prog_tag`.
