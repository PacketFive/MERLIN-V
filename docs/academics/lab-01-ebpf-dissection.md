# Lab 01 — eBPF Dissection

Module: 2
Effort tier: M
Prerequisites: Lab 00.
Design doc: [`docs/design/01-ebpf-comparative-analysis.md`](../design/01-ebpf-comparative-analysis.md).

## Learning objectives

After this lab you will be able to:

- Build a small eBPF program with `clang -target bpf` and link it
  with `libbpf`.
- Identify each component (program, map, helper, attach point) in the
  resulting ELF.
- Read the verifier log and explain why a program is accepted or
  rejected.
- Read JIT'd output for the host architecture and the eBPF bytecode
  for the program.
- Articulate, with concrete references, what BPF-V proposes to change.

## Background reading

- `bpf-next/Documentation/bpf/` — at minimum the `index.rst`,
  `verifier.rst`, and `clang-notes.rst`.
- `bpf-next/samples/bpf/*kern.c` — small, real eBPF programs.
- Calavera & Fontana, *Linux Observability with BPF*, chapters on the
  VM and verifier.

## Setup

This lab uses the `bpf-next` submodule's tooling. From the project
root:

```sh
cd bpf-next
make defconfig
make -j"$(nproc)" headers
cd tools/bpf/bpftool && make
cd ../resolve_btfids && make
cd ../../lib/bpf && make
```

Install dependencies your distro names differently:
`libelf-dev`, `libcap-dev`, `clang`, `llvm`, `pahole`.

## Tasks

### Task 1 — A "drop all" XDP eBPF program

Create `src/xdp_drop.bpf.c`:

```c
// SPDX-License-Identifier: GPL-2.0
#include <linux/bpf.h>
#include <bpf/bpf_helpers.h>

SEC("xdp")
int xdp_drop_all(struct xdp_md *ctx)
{
    return XDP_DROP;
}

char LICENSE[] SEC("license") = "GPL";
```

Build with:

```sh
clang -O2 -g -Wall -target bpf \
  -I bpf-next/tools/lib/bpf -I bpf-next/tools/include/uapi \
  -c src/xdp_drop.bpf.c -o build/xdp_drop.bpf.o
```

Use `bpf-next/tools/bpf/bpftool/bpftool prog dump xlated file build/xdp_drop.bpf.o`
to dump the eBPF instructions. Save the output to `build/xlated.txt`
and explain each instruction in `WRITEUP.md`.

### Task 2 — Load and attach

Use `bpftool` and a `veth` pair to attach:

```sh
sudo ip link add veth0 type veth peer name veth1
sudo ip link set veth0 up
sudo ip link set veth1 up

sudo bpf-next/tools/bpf/bpftool/bpftool \
    prog loadall build/xdp_drop.bpf.o /sys/fs/bpf/xdpdrop
sudo bpf-next/tools/bpf/bpftool/bpftool \
    net attach xdp pinned /sys/fs/bpf/xdpdrop/xdp_drop_all dev veth0
```

From the other end, send ping traffic into `veth1` and observe drops
with `ip -s link show veth0`.

Detach when done:

```sh
sudo bpf-next/tools/bpf/bpftool/bpftool net detach xdp dev veth0
```

### Task 3 — Read the JIT

```sh
sudo bpf-next/tools/bpf/bpftool/bpftool prog dump jited \
     pinned /sys/fs/bpf/xdpdrop/xdp_drop_all
```

Compare the JIT output to the xlated bytecode. Identify, instruction
by instruction, what the JIT did. Save to `build/jited.txt`.

> On `x86_64`, you will see x86 instructions. On a RISC-V host (or in
> a `qemu-system-riscv64` Linux instance built in Lab 00), you will
> see RISC-V instructions emitted by `arch/riscv/net/bpf_jit_*.c`.
> **Run this on both** if you can; explain the differences.

### Task 4 — Verifier interaction

Modify `xdp_drop.bpf.c` to introduce a deliberate verifier rejection:

```c
SEC("xdp")
int xdp_bad(struct xdp_md *ctx)
{
    void *data     = (void *)(long)ctx->data;
    /* Intentional: dereference without a bounds check */
    char  *p       = data;
    return *p;     /* will be rejected by the verifier */
}
```

Build and try to load it. Capture the verifier log with:

```sh
sudo bpf-next/tools/bpf/bpftool/bpftool -d \
    prog load build/xdp_bad.bpf.o /sys/fs/bpf/xdpbad
```

Save the log to `build/verifier_reject.log`. In `WRITEUP.md`, identify
which check rejected the program and link to the relevant code in
`bpf-next/kernel/bpf/verifier.c`.

### Task 5 — A map

Extend the program to count packets in a BPF\_MAP\_TYPE\_ARRAY of size
1. Print the count from user space using a small `libbpf`-based
loader (`src/xdp_count.c`). The lab skeleton provides the boilerplate
loader; you fill in the map lookup and the SEC("xdp") body.

### Task 6 — Design reflection

In `WRITEUP.md`, answer:

1. Which parts of this lab were *eBPF-specific work* that would
   disappear under BPF-V's "the bytecode is RISC-V" approach? Be
   specific: name files, tools, or steps.
2. Which parts would still be necessary under BPF-V? Why?
3. Where in the verifier log is the *abstract domain* visible? Cite a
   line number from `bpf-next/kernel/bpf/verifier.c`.

## Deliverables

- `src/xdp_drop.bpf.c`, `src/xdp_bad.bpf.c`, `src/xdp_count.bpf.c`
  and the user-space loader for Task 5.
- `build/xlated.txt`, `build/jited.txt`, `build/verifier_reject.log`.
- `WRITEUP.md`.

## Rubric

| Criterion | Points |
| --------- | ------ |
| Task 1: program builds and disassembles cleanly | 10 |
| Task 2: live attach to `veth0` demonstrably drops | 15 |
| Task 3: JIT output identified and explained | 15 |
| Task 4: verifier rejection log analysed correctly | 20 |
| Task 5: per-packet counter works | 25 |
| Task 6: design reflection accurate and specific | 10 |
| Writeup quality and AI attribution | 5 |
| **Total** | **100** |

## Common pitfalls

- Running the loader without `CAP_BPF`/`CAP_NET_ADMIN`.
- Mismatched kernel and `bpftool` versions; build `bpftool` from the
  submodule that you booted.
- Forgetting to delete pinned objects in `/sys/fs/bpf/` between
  attempts; leftover pins cause confusing `EEXIST`s.

## What's next

Lab 02 leaves the eBPF world and steps into RISC-V: you will read and
disassemble RISC-V code at the level required to *be* a verifier or a
JIT for it.
