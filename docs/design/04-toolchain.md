# 04 — Toolchain (Compiler, libbpfv, BTF-V, CO-RE-V)

Status: starter
Owner: PacketFive
Last reviewed: initial draft

This document records how BPF-V programs are produced and packaged in
user space, and the rationale for the compiler choice.

## 1. The compiler question

> *"eBPF uses LLVM to generate bytecode, we should use GCC as it can
> also generate code for Zephyr OS I believe — unless you recommend a
> different approach."*

### 1.1 The key insight that changes the question

eBPF needed a custom compiler backend because its bytecode is *not* a
target either GCC or LLVM had. So the BPF community wrote LLVM's BPF
backend (and later `gcc-bpf`). The maintenance cost of those backends
is non-trivial and they trail the host backends in features.

BPF-V's bytecode **is** RISC-V. Both GCC and LLVM have production
RISC-V backends already (`riscv32-*`, `riscv64-*`). No custom backend
is required. The only project-specific compiler work is:

1. A small **header set** (`<bpfv/*.h>`) declaring helper prototypes,
   `SEC("...")` macros, `ctx` types per program type.
2. A small **plugin or post-processing pass** that emits the BPF-V
   meta/maps/relocs sections.
3. Optional **clang/LLVM plugin** and **GCC plugin** for CO-RE-V
   relocations (similar to libbpf's `__builtin_preserve_access_index`).

That is dramatically less work than maintaining a backend.

### 1.2 Zephyr / GCC angle

Zephyr's primary toolchain (Zephyr SDK) is GCC-based. Zephyr does also
support LLVM/Clang for many boards, but GCC is the default and the
better-tested path on the boards we care about
(`esp32_devkitm`, `mpfs_icicle_kit`).

### 1.3 Recommendation: support both, default to GCC for kernel and
RTOS targets, treat Clang/LLVM as a co-equal alternative.

| Use case | Reference compiler | Notes |
| -------- | ------------------ | ----- |
| Linux in-kernel BPF-V programs (host) | GCC RISC-V | Aligns with kernel build infra; `riscv64-linux-gnu-gcc` is ubiquitous. |
| Zephyr / ESP32-C3 / MPFS bare metal | GCC RISC-V (Zephyr SDK) | SDK default; CI matches. |
| User-space BPF-V loaders / libbpfv | Either | Builds with both. |
| RFC submitter machines | Either | We commit a reproducible build for both. |
| Vendor accelerators | Whatever the vendor SDK ships | Often LLVM. Make sure both work. |

Reasons against picking only one:

- The kernel itself supports both GCC and Clang builds now
  (`make LLVM=1`); reflecting that posture in BPF-V signals respect for
  both communities.
- LLVM has a faster cadence on some RISC-V extensions (e.g. early RVV
  intrinsics, profile flags). Keeping it on the supported list lets us
  experiment without forking the toolchain story.
- Distros and customers will demand both.

### 1.4 Required compiler flags (per pinned profile)

For programs targeting **`bpfv-linux-rv64`** with
`riscv64-linux-gnu-gcc`:

```
-O2
-ffreestanding
-fno-stack-protector
-fno-asynchronous-unwind-tables
-fno-builtin
-mabi=lp64
-march=rv64imac_zicsr_zifencei
-mcmodel=medany
-fno-jump-tables         # verifier-friendlier
-fno-plt
-nostdlib
-Werror=implicit-function-declaration
```

For programs targeting **`bpfv-linux-rv64`** with
`clang --target=riscv64-unknown-linux-gnu`:

```
-O2
-ffreestanding
-fno-stack-protector
-mabi=lp64
-march=rv64imac_zicsr_zifencei
-mcmodel=medany
-fno-jump-tables
-nostdlib
```

For programs targeting **`bpfv-rtos-rv32`** (embedded), swap to
`riscv32-unknown-elf-gcc` (or `riscv32-zephyr-elf-gcc`) and
`-mabi=ilp32`, `-march=rv32imc_zicsr_zifencei` (add `_a` if the
target has the `A` extension). Use `-mno-relax` if the loader on
your target does not implement linker-relaxation fix-ups; the
verifier's relaxation policy is an open item — see
[06-verifier.md](06-verifier.md) §8.

### 1.5 What we are *not* doing

- Writing a new compiler backend.
- Forking GCC or LLVM.
- Shipping a custom assembler. (`gas` and `llvm-mc` are fine.)
- Inventing an IR. The IR *is* the ELF.

## 2. `libbpfv` (user-space runtime library)

A C library, dual-licensed GPL-2.0 / BSD-2-Clause to match `libbpf`'s
posture, providing:

- `bpfv_object__open(path)` — parse ELF, surface programs/maps.
- `bpfv_object__load(obj)` — issue `BPFV_BTF_LOAD`, `BPFV_MAP_CREATE`,
  `BPFV_PROG_LOAD` for each component.
- `bpfv_program__attach_xdp(prog, ifindex)` etc. — hook attachment.
- Map accessors mirroring libbpf.
- A skeletons generator (`bpfvtool gen skeleton`) so programs can be
  embedded in a host binary, libbpf-style.

`libbpfv` lives in `tools/lib/bpfv/` (mirroring `tools/lib/bpf/`).
We *will not* statically link or vendor `libbpf` into `libbpfv`, but
where the in-kernel implementation reuses BPF maps, `libbpfv` may call
`libbpf` for map manipulation as a convenience layer. Decision deferred
to RFC review.

## 3. BTF-V

BTF-V is BTF with two additions:

- A new tag space for RISC-V-specific relocations (CO-RE-V field
  offsets keyed on the running kernel/firmware's RISC-V ABI choices).
- A small program-type-table describing each `ctx` struct (field
  names, types, byte offsets) so the verifier and the loader agree on
  the contract.

`pahole`'s BTF emitter is the upstream truth for BTF; BTF-V additions
will be proposed as a `pahole` patch series in parallel with the kernel
RFC.

## 4. CO-RE-V

Same conceptual mechanism as eBPF CO-RE:

1. Programs reference kernel types via `BPFV_CORE_READ(...)` etc.
2. The compiler emits a CO-RE-V relocation entry naming the field by
   `(type, member)` plus the access kind.
3. At load time the kernel/firmware resolves the relocation against the
   live BTF-V and patches the program image.

For BPF-V the patching happens on RISC-V instructions (e.g.,
rewriting a 12-bit immediate offset in an `ld` instruction).

## 5. `bpfvtool`

A CLI patterned on `bpftool`:

```
bpfvtool prog load file.bpfv.o /sys/fs/bpf/foo
bpfvtool prog show
bpfvtool prog dump xlated id 7              # disassemble RISC-V
bpfvtool prog dump jited id 7               # on non-RV hosts only
bpfvtool map show
bpfvtool btf-v dump file foo.btf.v
bpfvtool gen skeleton file.bpfv.o > file.skel.h
```

Disassembly leans on `binutils`' `objdump -d -Mriscv` rather than
re-implementing a disassembler.

## 6. Open items

- Decide whether to ship one shared header set (`<bpfv/*.h>`) for both
  GCC and Clang, or two with `#ifdef __clang__` shims.
- Decide CO-RE-V reloc record encoding (raw BTF reuse vs. dedicated
  section).
- Decide skeleton ABI (drop-in pattern matching libbpf's, or distinct?).
- Decide whether `libbpfv` taps `libbpf` for shared map APIs.
