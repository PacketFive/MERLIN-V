# Lab 05 — `merlin-objtool`

The Lab 05 statement is in `docs/academics/lab-05-merlin-objtool.md`.

This lab's starter code is **not yet committed**. Students start from
the user-space prototype at `tools/merlin-objtool/` (in the parent
project) and re-implement the parts marked `TODO:STUDENT` in this
lab's lab-specific copy. The instructor distributes the lab-specific
copy along with a tarball; until then, point at the parent prototype:

```bash
ls $(git rev-parse --show-toplevel)/tools/merlin-objtool/
```

When the lab-specific scaffold lands, it will live here as:

```
lab-05-objtool/
├── README.md           # lab-specific instructions
├── Makefile
├── src/
│   ├── objtool.h       # PROVIDED
│   ├── objtool.c       # SKELETON — students implement
│   └── main.c          # PROVIDED
└── tests/
    ├── good/           # ELF blobs that should pass
    └── bad/            # ELF blobs that should fail (with reason)
```
