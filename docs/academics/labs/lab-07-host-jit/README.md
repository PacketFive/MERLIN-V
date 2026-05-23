# Lab 07 — Host JIT (RV32 → x86_64)

The Lab 07 statement is in `docs/academics/lab-07-host-jit-x86_64.md`.

This is the biggest single lab and its scaffold is **not yet
committed**. Students start from the user-space JIT at
`tools/merlin-jit-x86_64/`:

```
tools/merlin-jit-x86_64/
├── emit.c    # x86_64 byte emitter — REFERENCE
├── emit.h
├── jit.c     # translation pass — REFERENCE
├── jit.h
└── ...
```

When the lab-specific scaffold lands, students will be given:

- `emit.{h,c}` PROVIDED (the byte emitter is fiddly and not the
  pedagogical point).
- `jit.c` SKELETON — students implement the per-RV-instruction
  translation cases.
- A reference harness that mmap's an RX region, runs the JIT'd
  function against a context buffer, and prints the return value.

The grading bar is the same five "interpreter" tests from Lab 03,
plus three additional ones the autograder uses.
