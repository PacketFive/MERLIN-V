# Lab 06 — Pass-through JIT

The Lab 06 statement is in `docs/academics/lab-06-passthrough-jit.md`.

This lab's starter code is **not yet committed**. The exercise is
small enough that students can derive it from the user-space prototype
at `tools/merlin-jit-x86_64/jit.c` (the prologue/epilogue path), the
kernel module at `kernel/merlin/jit/pass_through.c`, and the Zephyr
runtime at `zephyr/merlin/src/runtime.c::merlin_runtime_install`.

When the lab-specific scaffold lands, it will live here as:

```
lab-06-passthrough/
├── README.md
├── Makefile
├── src/
│   ├── passthrough.h
│   ├── passthrough.c   # SKELETON — students implement
│   └── main.c
└── tests/
```

The graded deliverable is a function that takes a verified RV32 byte
buffer and a host RV32 (or RV64) CPU, allocates an executable region,
copies the bytes, flushes the I-cache, and returns a function pointer
that the test harness invokes against a small context buffer.
