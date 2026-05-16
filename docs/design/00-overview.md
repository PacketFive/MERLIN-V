# 00 — BPF-V Overview

Status: starter
Owner: PacketFive
Last reviewed: initial draft

## 1. One-paragraph summary

BPF-V is a small, verifiable, in-kernel JIT virtual machine whose
**bytecode is a restricted profile of the RISC-V ISA** (RV32I/IM/IMC and
RV64I/IM/IMC/IMAC, with optional well-defined extensions).

A JIT step exists in both eBPF and BPF-V — verified bytecode does not
execute as bytecode; it executes as machine code. The question is
*which* machine code, and how much translation work that takes:

- **eBPF's bytecode is a custom 64-bit RISC-like ISA that no commodity
  CPU implements in silicon.** Every host — x86\_64, arm64, ppc64le,
  s390x, *even RISC-V* — must run a per-architecture translator
  (`arch/$ARCH/net/bpf_jit_comp.c`) over every program before
  execution. Translation is unavoidable, always.
- **BPF-V's bytecode is RISC-V.** On a RISC-V host or accelerator, the
  verified image *is* the executable: the "JIT" reduces to a
  pass-through step (verify → relocate → I-cache flush → jump),
  hardware-native at the instruction level — 1:1 with no translation.
  On non-RISC-V hosts (x86\_64, arm64, …) BPF-V still needs a real
  JIT, structurally similar to the eBPF JITs upstream — but its input
  is standard RISC-V machine code rather than a bespoke bytecode.

The same BPF-V image is loadable into Linux, into RISC-V-based
SmartNIC firmware over PCIe/CXL/UALink, and into Zephyr RTOS on
microcontrollers (e.g. ESP32-C3, MPFS Icicle Kit).

## 2. Why a new VM

eBPF's bytecode is a custom 64-bit RISC-like ISA that exists because, in
2014, there was no widely available, free, simple, 64-bit RISC ISA with
production toolchains. That precondition no longer holds: RISC-V is now
ratified, ABI-stable, multi-vendor, and supported in GCC, LLVM, glibc,
musl, Linux, Zephyr, FreeRTOS, and the major silicon vendors.

The cost eBPF pays for its custom ISA is concrete:

1. A bespoke LLVM backend (BPF target) and a parallel GCC backend
   (`gcc-bpf`), neither of which evolves at the rate of the host
   backends.
2. Translation everywhere the program runs — including on hardware
   offload targets, which must implement (or emulate) eBPF semantics in
   silicon or in NIC firmware.
3. Verifier and JIT carry the cost of being the *only* checker /
   compiler for that ISA.

BPF-V's hypothesis is that reusing RISC-V eliminates (1) entirely,
turns (2) into a no-op on the increasingly common case of a RISC-V
target, and shrinks (3) to a verifier-only effort plus a thin JIT for
non-RISC-V hosts.

## 3. Goals (G) and non-goals (NG)

### G1. Safety equivalent to eBPF
Programs are statically verified before execution. Memory safety, bounded
loops or termination, and helper-only side effects must hold under the
same threat model as eBPF (untrusted but authenticated loader, possibly
unprivileged with capability gating).

### G2. RISC-V as the wire format
A BPF-V object is an ELF containing standard RISC-V machine code, BTF-V
type info, relocations, a map descriptor table, and a small BPF-V meta
section. No new opcode space, no custom register file.

### G3. 1:1 hardware-native execution on RISC-V targets
Both eBPF and BPF-V need a JIT step — verified code does not run as
bytecode. The difference is what that step has to do.

On a RISC-V execution target (host CPU, MCU, NIC core, CXL
accelerator) the BPF-V "JIT" is a pass-through: verify, relocate,
flush I-cache, jump. The bytecode *is* the host's native instruction
stream; there is no instruction translation.

eBPF, by contrast, *always* translates: its bytecode is a custom
64-bit RISC-like ISA, and no commodity silicon implements it. Even on
a RISC-V host, eBPF goes through
`bpf-next/arch/riscv/net/bpf_jit_comp.c` to translate every program.
The translation cost is therefore eliminated for BPF-V on the
increasingly common case of a RISC-V target, and reduced for offload
to a hardware re-installation step on a RISC-V SmartNIC or
accelerator.

### G4. Cross-OS reusability
The same object file targets Linux (in-kernel JIT VM), Zephyr (RTOS
execution engine), and bare-metal NIC/accelerator firmware. The kernel
ABI obviously differs per OS; the bytecode does not.

### G5. Toolchain reuse
Programs are compiled by stock RISC-V GCC or Clang, with a small set of
project-defined attributes and a `libbpfv`-equivalent runtime. No
project-maintained compiler backend.

### G6. Upstreamability
The Linux kernel components target `bpf-next` and `net-next` and follow
the standard kernel process. The in-tree path is `kernel/bpfv/` (TBD,
see [03-kernel-interfaces.md](03-kernel-interfaces.md)).

### NG1. Drop-in eBPF replacement
BPF-V is *not* binary-compatible with eBPF. It is a parallel facility
that may share verifier and map infrastructure where it makes sense, but
the bytecode, ABI, and tooling are distinct.

### NG2. Source-level compatibility with arbitrary C
Like eBPF, only a restricted dialect of C compiles to a valid BPF-V
object **in the Linux in-kernel default profile.** The constraints
come from the verifier (see [06-verifier.md](06-verifier.md)) and
from the chosen RISC-V profile (see
[02-isa-and-bytecode.md](02-isa-and-bytecode.md)).

This restriction is **profile-relative**, not bytecode-relative.
Other execution contexts (RISC-V SmartNIC / accelerator firmware,
Zephyr / MCU targets, optional user-mode sandbox) define their own
verifier profiles that admit substantially more of ANSI / GNU C. See
§7 of this document and
[06-verifier.md](06-verifier.md) §7 for the per-context relaxation
model.

### NG3. Replacing XDP, AF\_XDP, sockmap, etc.
BPF-V is an execution engine, not a hook surface. The hook surfaces
(XDP, TC, kprobe, tracepoint, sockmap, sched\_ext, …) are reused from
the kernel — initially by *cohabiting* with eBPF program types, and
later by exposing parallel `BPF_V_PROG_TYPE_*` definitions where the
program needs RISC-V-only semantics.

## 4. Threat and execution model

- **Loader trust:** A privileged user-space process (`CAP_BPF` analog,
  TBD `CAP_BPFV`) submits a signed or capability-gated object. The
  kernel verifies and either rejects or installs.
- **Execution privilege:** In-kernel programs run with kernel privilege
  on the host CPU after verification. On accelerators, programs run in
  U-mode within a verified sandbox supervised by accelerator firmware
  (the "BPF-V monitor", see [07-jit-and-offload.md](07-jit-and-offload.md)).
- **Memory access:** All pointer derivations are tracked by the
  verifier from a small set of typed roots (context pointer, map values,
  packet bounds, helper return values). Random pointer construction is
  forbidden at the bytecode level by ISA-profile restrictions plus
  static analysis.
- **Side effects:** All externally visible side effects go through
  registered helpers invoked via `ecall` (see
  [02-isa-and-bytecode.md](02-isa-and-bytecode.md) §ABI).

## 5. Glossary

- **BPF-V profile** — the subset of RISC-V instructions, CSRs, and
  memory orderings that are permitted in a BPF-V program.
- **BTF-V** — type information format for BPF-V; conceptually BTF
  extended with RISC-V-specific relocation kinds. (See
  [04-toolchain.md](04-toolchain.md).)
- **CO-RE-V** — "Compile Once, Run Everywhere" for BPF-V: type-relative
  relocations against the running kernel/firmware.
- **BPF-V monitor** — minimal trusted runtime on an accelerator core
  that enforces the BPF-V sandbox without a full Linux kernel.
- **Host JIT** — the translator that emits non-RISC-V host code from a
  verified BPF-V image (only used on x86\_64, arm64, etc.).
- **Pass-through JIT** — the trivial "JIT" used on RISC-V hosts: verify,
  relocate, map, jump. No instruction translation.

## 6. Open questions

These are tracked here so they survive into the RFC. Each links into a
deeper-dive document:

- Memory model on `bpfv-linux-rv64`: default to RVWMO and require
  programs to use explicit fences, or pin a Ztso-required variant
  profile? Probably default RVWMO with Ztso opt-in if hardware
  advertises it — see
  [02-isa-and-bytecode.md](02-isa-and-bytecode.md) §7.
- Vector (RVV 1.0): per-program flag inside `bpfv-linux-rv64`
  post-RFC, or a new profile name (`bpfv-linux-rv64v`)? Out of
  scope for RFC v1.
- `A`-extension AMOs in `bpfv-linux-rv64`: allow any
  typed-pointer atomic, or require helpers for cross-CPU ordering
  critical sections? — see [06-verifier.md](06-verifier.md) §8.
- Linker relaxation policy: verify before or after relaxation, or
  require `-mno-relax` from the compiler.
- Co-existence with eBPF: shared map infra (yes, planned), shared
  verifier core (Option A as a follow-up to Option B v1) — see
  [06-verifier.md](06-verifier.md) §4 and
  [03-kernel-interfaces.md](03-kernel-interfaces.md) §5.

The following previously-open questions are now **decided**
(recorded here for traceability):

- ~~Which RISC-V profile do we pin?~~ → two project profiles,
  `bpfv-linux-rv64` and `bpfv-rtos-rv32`. See
  [02-isa-and-bytecode.md](02-isa-and-bytecode.md) §1.
- ~~Which extensions are mandatory / optional / forbidden?~~ → §3
  of `02-isa-and-bytecode.md`, pinned per profile.
- ~~Helper invocation: ecall with a7, or jump-table relocation?~~
  → ecall + a7 source-level encoding, loader rewrites to direct
  call at install time. See
  [02-isa-and-bytecode.md](02-isa-and-bytecode.md) §6.
- ~~Verifier strategy~~ → subset + abstract interpretation, with
  Option B (standalone) for RFC v1 and Option A (shared
  abstract-domain module) as a follow-up. See
  [06-verifier.md](06-verifier.md) §4.
- ~~Toolchain default~~ → GCC and Clang both supported as
  first-class. See [04-toolchain.md](04-toolchain.md) §1.

## 7. What BPF-V is not (and what it could become)

A natural question — and one that came up explicitly during this
project's design conversation — is:

> If we drop the "restricted dialect of C" rule and let programs use
> full ANSI / GNU C, can a BPF-V VM run arbitrary applications
> in-kernel, not just packet filters and tracing?

The short answer is **no in the Linux host kernel, yes on accelerator
firmware and on RTOS / MCU targets** — and the reason is illuminating
enough to record here.

### 7.1 The C dialect is a symptom, not the cause

The C-dialect restriction in eBPF (and in BPF-V's default in-kernel
profile) is downstream of the verifier. The verifier rejects programs
that use constructs whose safety it cannot prove; the toolchain then
documents "don't use these constructs" as a dialect. **Relaxing the
dialect changes nothing unless the verifier is also relaxed.** And
"relax the verifier" is the question we actually need to answer.

### 7.2 The four real constraints on in-kernel programs

Even with a maximally permissive verifier, a Linux in-kernel program
faces four constraints that are not properties of the bytecode:

1. **Execution context.** Most kernel hooks run in atomic context: no
   sleeping, no page faults, no blocking I/O. This rules out
   `malloc`, `read`, `write`, `printf`, `mutex_lock`, `sleep`, and
   anything that calls into a real syscall. No verifier change fixes
   this; it is a property of *when* the program runs.
2. **No syscalls.** A program executing in the kernel cannot make
   Linux syscalls — the kernel does not call itself that way. Any
   facility user-space programs reach via libc is unavailable unless
   it is exposed as a helper / kfunc. "Running apps" in-kernel
   therefore means "running apps written against a helper API,"
   not "running glibc-linked binaries."
3. **No process abstraction.** No `fork`, no threads, no signals, no
   per-process address space, no per-program lifecycle resembling a
   POSIX process.
4. **Verifier cost and verifier blast radius.** Every byte of
   accepted code is in the kernel's effective TCB. A verifier bug is
   a kernel CVE. Maintainers will (rightly) push back on widening
   the surface beyond what the verifier can robustly handle.

Relaxing the C dialect is necessary but not sufficient; you must
also expand the verifier's accepted programs, expand the helper
surface, *and* change the execution context the program runs in.

### 7.3 Where BPF-V already has more headroom than eBPF

Even within the Linux in-kernel constraints, BPF-V can comfortably
support more than eBPF does today:

- Larger programs (eBPF caps at ~1M instructions post-verification).
- Larger stacks (eBPF: 512 bytes; BPF-V default profile aim: 8–64
  KB — see [02-isa-and-bytecode.md](02-isa-and-bytecode.md) §5).
- Real bounded recursion as a first-class feature.
- SIMD via verified RVV (vector extension) on hooks where it makes
  sense — see [02-isa-and-bytecode.md](02-isa-and-bytecode.md) §3.
- "Sleepable by default" on hook surfaces that already permit it;
  eBPF treats sleepable as a per-program flag.
- Persistent program model: long-lived programs with state, designed
  for hot attach/detach.

The trajectory in the eBPF world is already moving this direction
(`sched_ext`, `struct_ops`, `bpf_arena`, sleepable programs,
`fuse-bpf`). BPF-V can start from that direction explicitly.

### 7.4 Where the answer flips to "yes"

There are two execution contexts in which BPF-V legitimately *is* an
application runtime, not a kernel-extension language:

1. **RISC-V SmartNICs, DPUs, PCIe / CXL / UALink accelerators.**
   The "kernel" is firmware — typically a small RTOS or microkernel
   on a RISC-V core. The verifier plays the role that a vendor SDK
   normally plays: keeping untrusted programs from corrupting
   trusted code. The execution context is much more permissive than
   the Linux kernel — often single-tenant, long-running, sleep-able,
   with large memory. **A customer's "app" running on a NIC is a
   BPF-V program.** This is the offload story made concrete.
2. **Zephyr / FreeRTOS / bare metal on MCUs.** No MMU, cooperative
   or simple preemptive scheduling, single address space anyway.
   "Kernel vs application" is largely vocabulary. BPF-V on an MCU
   is a way to load application logic onto a constrained device
   with verifier-guaranteed safety in place of developer discipline:
   field-updatable sensor pipelines, real-time control loops, BLE
   policy, sensor fusion.

### 7.5 Architectural picture

```
                  permissiveness →

  Linux host       eBPF     BPF-V (Linux default)   BPF-V (sleepable, large)
  kernel:         |--------|----------------------|------ still NOT an app platform

  NIC / DPU                            BPF-V (offload profile)
  firmware:                            |----------------- IS the app runtime

  Zephyr / MCU                                          BPF-V (RTOS profile)
                                                        |------ IS the app runtime
```

Same image format, same toolchain, same verifier core, same JIT
machinery. **Different execution contexts, different relaxation
levels of the verifier and the helper set.**

### 7.6 The optional third tier — user-mode BPF-V

A third execution context, not in the current design plan but worth
recording here: run the BPF-V VM in a user-space sandbox process
(SFI-style or `seccomp`-confined). The verifier accepts a more
permissive program if the loader requests the user-mode profile; the
helper set is broader (real `open`/`read`/etc. mediated by the
sandbox). The same image can then run in-kernel where the hook
allows it and in user space where the hook doesn't, with the loader
selecting which verifier profile is enforced.

This is conceptually close to what NaCl, WebAssembly + WASI, and
gVisor each do in their own ways. The BPF-V twist is that the *same*
ELF image is the wire format across all of these contexts. Tracked
as an open item; see [06-verifier.md](06-verifier.md) §7.

### 7.7 Implications for the design plan

- The **Linux in-kernel default profile stays conservative** —
  eBPF-shaped restrictions, plus the RISC-V-specific ISA-profile
  rules in [02-isa-and-bytecode.md](02-isa-and-bytecode.md). This is
  the right shape for an RFC v1 that does not scare BPF
  maintainers.
- **Sleepable / large / RTOS / offload / user-mode** profiles are
  named, explicit relaxations of that default — each with a
  documented helper set, verifier step cap, and stack budget. They
  are opt-in, capability-gated, and not the default.
- The verifier architecture must therefore be **parameterised by
  profile** from day one, even if only the conservative profile is
  implemented at first. See [06-verifier.md](06-verifier.md) §7.

### 7.8 What this changes in NG2

The non-goal "Source-level compatibility with arbitrary C" (§3 NG2
above) is preserved for the Linux in-kernel default profile. On
NIC/accelerator firmware and on RTOS/MCU targets, that non-goal is
**relaxed** to: "as much of ANSI/GNU C as the verifier for the
chosen execution-context profile can prove safe." The line is
context-dependent, not bytecode-dependent.

## 8. Advantages on non-RISC-V hosts (and what we are NOT claiming)

The clearest BPF-V wins are on RISC-V execution targets, where the
JIT collapses to a pass-through and offload to RISC-V silicon is
1:1. A reasonable reader will then ask: **on an x86\_64 or arm64
host, where BPF-V still needs a real translating JIT, what does
BPF-V offer over eBPF?** This section answers honestly.

### 8.1 What we are NOT claiming

We are **not** claiming that BPF-V is faster than eBPF on non-RISC-V
hosts at steady state. Both stacks JIT once at load time and emit
comparable native code for comparable source programs. We expect any
per-packet or per-event cycle-count difference on x86\_64 and arm64
to be statistically indistinguishable and dominated by JIT
engineering choices rather than by bytecode choice. The evaluation
plan in
[`01-ebpf-comparative-analysis.md`](01-ebpf-comparative-analysis.md) §5
includes this comparison **explicitly to set the record straight**,
not to demonstrate a win.

We are also not claiming smaller in-kernel C in the short term. The
first BPF-V kernel drop *adds* `kernel/bpfv/` alongside `kernel/bpf/`.
Net code-size wins (if any) come later, from sharing the abstract-
interpretation infrastructure with the eBPF verifier (Option A in
[`06-verifier.md`](06-verifier.md) §4).

### 8.2 What we DO claim on non-RISC-V hosts

The wins below are real on every host, not just RISC-V. They are
ordered from "strongly defensible" to "soft but real."

**A. Toolchain reuse — no project-specific compiler backend.**
The eBPF subsystem maintains an LLVM BPF target and a parallel
`gcc-bpf` backend. Both backends trail the host backends in
features, attributes, sanitisers, debug-info, and LTO. BPF-V
deletes both forever and inherits the upstream RISC-V GCC and Clang
backends, which are mainstream targets evolving for non-BPF reasons.
A BPF-V program loaded on an x86\_64 host is therefore compiled by
the same backend that builds Fedora-RISCV and Ubuntu-RISCV — a
backend with many more eyes on it than any BPF-only compiler will
ever have.

**B. One wire format across all targets.** A single `.bpfv.o` is
the unit of distribution from an x86\_64 build host to a RISC-V
SmartNIC to a Zephyr MCU. eBPF SmartNIC offload today requires the
NIC vendor to re-translate eBPF bytecode to the NIC's own ISA
(NFP, microengines, etc.) — a second translation downstream of the
JIT. BPF-V's image needs no such re-translation on a RISC-V NIC.
The benefit accrues even when the *host* compiling and loading the
image is non-RISC-V.

**C. Verifier consolidation (long-term, conditional).** The Linux
kernel currently maintains the eBPF verifier as the *only* checker
for the eBPF ISA. If BPF-V ships its verifier with the abstract
domain factored as a shared module (Option A in
[`06-verifier.md`](06-verifier.md) §4), the kernel ends up with one
abstract-interpretation engine parameterised by two decoders rather
than two parallel engines. This is a maintainability claim, not a
runtime-perf claim. Whether it materialises depends on Option A
landing.

**D. Larger, more capable programs by construction.** eBPF's caps
(1 M post-verification instructions, 512-byte stack, no general
recursion) are partly historical and partly verifier-engineering
choices. BPF-V's profile-parameterised verifier (see §7 of this
document and [`06-verifier.md`](06-verifier.md) §7) is designed to
lift those caps deliberately *per context*. eBPF is moving the same
direction (`bpf_arena`, sleepable, `sched_ext`) but against legacy.
On any host, BPF-V starts at the destination.

**E. Standard developer ergonomics.** `objdump -Mriscv -d` works
out of the box on a BPF-V program. GDB can step through it under
QEMU. `perf annotate` on a JITed BPF-V program shows familiar
RISC-V; `perf annotate` on eBPF shows host code with no mapping
back to bytecode. Modest individually; accumulates.

**F. ABI compatibility with C calling conventions.** BPF-V uses the
RISC-V psABI: arguments in `a0..a7`, return in `a0`, 16-byte
stack alignment. Helpers are callable with normal C calling
conventions, JIT register allocation operates on a familiar register
file with a documented psABI, and writing helpers in the kernel
looks like writing any other C function. eBPF defines a smaller
custom ABI that necessitates dedicated trampolines.

**G. Future compiler features come for free.** When RVV vector
intrinsics, Zb\* bit-manipulation, or future RISC-V extensions
stabilise in GCC and Clang, BPF-V programs that opt into them (via
the profile selection in §7) inherit them the day the verifier
learns the new instructions. eBPF SIMD or analogous features
require new backend work each time.

**H. Pedagogy and research surface.** A graduate student can write
a BPF-V verifier or JIT for a class using the RISC-V Instruction
Set Manual and the psABI document as the spec. For eBPF the
*de facto* spec is the kernel verifier's source. This sounds
peripheral and is not — see [`docs/academics/`](../academics/) for
how this project leans on it.

### 8.3 Honest ranking

If asked to pick the three claims that justify the project to a
sceptical kernel maintainer who only runs x86 hardware, in priority
order:

1. **A — toolchain reuse.** Permanent, host-independent, large.
2. **B — one wire format.** Real even when the host is x86; gets
   stronger the moment a RISC-V SmartNIC is in the picture.
3. **D — profile-parameterised verifier.** The architectural unlock
   that lets the design serve "BPF for the next 15 years" without
   refighting the same battles.

C, E, F, G, H are real but secondary; they should appear in the
paper, not at the top of the RFC cover letter.

### 8.4 Three-sentence summary (for the RFC cover letter)

> BPF-V's primary value proposition is architectural and
> organisational, not runtime-performance on commodity hosts. It
> eliminates a class of work the kernel BPF community has to do —
> maintaining bespoke compiler backends and a bespoke verifier-only
> ISA — in exchange for adopting a ratified, vendor-neutral ISA
> whose tooling is maintained for unrelated reasons. On a RISC-V
> execution target this also yields a 1:1 pass-through path with no
> instruction-translation cost; on a non-RISC-V host the runtime
> cost is comparable to eBPF and the wins are in the toolchain, the
> verifier-engineering surface, and the cross-target wire format.
