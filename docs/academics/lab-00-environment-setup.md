# Lab 00 — Environment Setup

Module: 1
Effort tier: S
Prerequisites: a Linux developer machine (x86\_64) with sudo.
Design doc: none directly; this is the foundation for everything that
follows.

## Learning objectives

After this lab you will be able to:

- Install and verify a RISC-V cross-toolchain in both GCC and Clang
  variants.
- Boot a minimal `qemu-system-riscv64` Linux system with networking.
- Build a kernel from this repository's `bpf-next` submodule.
- Build and run a `Hello World` for `qemu_riscv32` under Zephyr.
- Initialise this repository's submodules and explain the submodule
  policy.

## Background reading

- `docs/academics/README.md` — course intro.
- `docs/design/05-reference-platforms.md` — what the course is
  ultimately targeting.

## Tasks

### Task 1 — Repository and submodules

```sh
git clone <your fork URL> bpfv-course
cd bpfv-course
git submodule update --init --depth 1 net-next bpf-next
```

Document, in `WRITEUP.md`:
- The exact submodule pointers (commit hashes) you ended up with.
- Why the project uses these as **reference-only** (hint: see
  `docs/design/README.md`).

### Task 2 — RISC-V cross-toolchains

Install both:

- **GCC**: `riscv64-linux-gnu-gcc` (rv64) and `riscv32-unknown-elf-gcc`
  (rv32, baremetal). On Debian/Ubuntu, the packages are
  `gcc-riscv64-linux-gnu` and `gcc-riscv64-unknown-elf`.
- **Clang**: a recent `clang` with `--target=riscv64-unknown-linux-gnu`
  support (Clang 16+).

Verify with the supplied `tools/check_toolchains.sh` (provided in the
lab skeleton). On success the script prints both toolchain triples and
their versions and exits 0.

Hand-build the following one-liner test:

```sh
cat > /tmp/hello.c <<'EOF'
int main(void) { return 42; }
EOF

riscv64-linux-gnu-gcc -O2 -static /tmp/hello.c -o /tmp/hello.rv64
clang --target=riscv64-unknown-linux-gnu \
      -fuse-ld=lld -static -O2 /tmp/hello.c -o /tmp/hello.rv64.clang
file /tmp/hello.rv64
file /tmp/hello.rv64.clang
```

Both `file(1)` outputs must say `ELF 64-bit LSB executable, UCB RISC-V`.

### Task 3 — QEMU user mode

Run both binaries under user-mode QEMU:

```sh
sudo apt install qemu-user-static
qemu-riscv64-static /tmp/hello.rv64       ; echo $?     # → 42
qemu-riscv64-static /tmp/hello.rv64.clang ; echo $?     # → 42
```

### Task 4 — QEMU system mode (`qemu-system-riscv64`)

Goal: boot a Linux system on the `virt` machine.

```sh
sudo apt install qemu-system-misc opensbi u-boot-qemu
# Fetch a prebuilt RV64 rootfs (cloud image or buildroot). The lab
# skeleton ships a script tools/fetch_rootfs.sh that downloads a
# pinned Buildroot image.
./tools/fetch_rootfs.sh

qemu-system-riscv64 \
  -nographic \
  -machine virt -m 2G -smp 4 \
  -bios /usr/lib/riscv64-linux-gnu/opensbi/generic/fw_jump.elf \
  -kernel build/Image \
  -append "root=/dev/vda console=ttyS0" \
  -drive file=build/rootfs.ext4,format=raw,if=virtio \
  -netdev user,id=net0 -device virtio-net-device,netdev=net0
```

Acceptance: a login prompt appears, and `uname -m` inside the guest
prints `riscv64`.

### Task 5 — Kernel build from `bpf-next` submodule

```sh
cd bpf-next
make ARCH=riscv CROSS_COMPILE=riscv64-linux-gnu- defconfig
make ARCH=riscv CROSS_COMPILE=riscv64-linux-gnu- olddefconfig
make ARCH=riscv CROSS_COMPILE=riscv64-linux-gnu- -j"$(nproc)" Image modules
```

Copy `arch/riscv/boot/Image` into your QEMU bring-up directory and
re-run Task 4 with this kernel. Confirm `uname -r` reports the kernel
version from the submodule.

> Submodule policy reminder: this lab does **not** commit any changes
> inside `bpf-next/` or `net-next/`. You may build there. You may not
> push there. Submission only includes files outside the submodules.

### Task 6 — Zephyr SDK and `qemu_riscv32`

```sh
# Install west and Zephyr per the upstream getting-started guide:
#   https://docs.zephyrproject.org/latest/develop/getting_started/

west init zephyrproject
cd zephyrproject
west update
west zephyr-export
west build -p auto -b qemu_riscv32 zephyr/samples/hello_world
west build -t run
```

Acceptance: the QEMU console prints `Hello World! qemu_riscv32`.

### Task 7 — Sanity selftest

The lab skeleton ships `tools/grade.sh`. Run it; it should produce a
report covering Tasks 1–6 and exit 0.

## Deliverables

- `WRITEUP.md` covering tasks 1–6 (≤ 1500 words).
- `tools/grade.sh` autograder log.
- Submodule pointer commit hashes recorded in the writeup.

## Rubric

| Criterion | Points |
| --------- | ------ |
| Submodules correctly initialised, hashes recorded | 10 |
| GCC and Clang RISC-V toolchains both functional | 20 |
| `qemu-system-riscv64` boots to login | 20 |
| Kernel built from `bpf-next` and booted in QEMU | 25 |
| Zephyr `hello_world` runs on `qemu_riscv32` | 20 |
| Writeup quality and AI attribution if used | 5 |
| **Total** | **100** |

## Common pitfalls

- Mixing `riscv64-unknown-elf-gcc` (baremetal, newlib) and
  `riscv64-linux-gnu-gcc` (glibc-aware) toolchains. Use the right one
  for the target.
- Forgetting `-static` when running under `qemu-user-static`.
- Building the kernel without `CROSS_COMPILE`; the default `gcc` on
  an x86 box will silently produce x86 objects until linking blows up.
- Old QEMU versions lacking the `virt` machine's RISC-V improvements.
  Use Debian Bookworm or newer / Ubuntu 22.04 or newer.

## What's next

Lab 01 turns this working setup into a hands-on tour of eBPF: build a
real program, watch the verifier accept or reject it, observe the
JIT'd code, and read the kernel-side data structures.
