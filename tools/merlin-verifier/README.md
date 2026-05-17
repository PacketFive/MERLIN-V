# `merlin-verifier` — prototype

The second user-space code prototype of MERLIN-V.  An abstract-
interpretation verifier that walks the `.text.merlin.*` sections of a
MERLIN-V ELF object and decides whether the bytecode is acceptable
under the rules in
[`../../docs/design/06-verifier.md`](../../docs/design/06-verifier.md).

This prototype implements the Option-B strategy
([`../../docs/design/06-verifier.md`](../../docs/design/06-verifier.md) §4) —
a standalone MERLIN-V verifier independent of `kernel/bpf/`.  It is
the user-space embodiment of the same algorithm the kernel-side
verifier will run; this gives us a fast iteration loop, a place to
prove the design, and a test fixture for `proto-kernel-loader`.

## Scope (v0)

The verifier checks:

1. **Profile compliance.** Every decoded instruction belongs to the
   linux-rv64 default profile.  CSR, privileged (mret/sret/wfi/
   sfence.vma), `fence.i`, `ebreak`, and FP instructions are rejected.
2. **Helper ABI.** Every `ecall` must be preceded by a constant load
   into `a7` (`addi a7, x0, K`) where `K` is a registered helper ID
   in the program-type allowlist.  Unknown `a7` at `ecall` is
   rejected.  Disallowed helper IDs are rejected.
3. **Pointer provenance.** Every `load`/`store` must use an address
   whose base register holds a *typed* root: `PTR_CTX`, `PTR_STACK`,
   or `PTR_HELPER_RET`.  Memory access through `UNKNOWN` or `CONST`
   bases is rejected.
4. **No back-edges.**  Loops are not yet supported (v0); any branch
   or `JAL` with a negative immediate is rejected with a clear
   message that bounded loops are a follow-up.
5. **Bounded indirect calls.**  `JALR` is permitted only via `ra`
   (the return-from-function pattern).  kfunc-slot resolution is a
   follow-up.

The abstract register domain is:

```
   {UNKNOWN, CONST(u64), PTR_CTX{off}, PTR_STACK{off}, PTR_HELPER_RET{helper_id}}
```

— deliberately less precise than the full eBPF `tnum + range +
pointer-typing` lattice but sufficient to express the rules above.

## Not yet implemented (post-v0)

- Real CFG with join points / widening / fixpoint iteration.
- Bounded-loop verification (via the `merlin_loop()` helper).
- Stack-frame discipline (sp delta tracking).
- kfunc resolution.
- Bit-level tnum tracking.
- CO-RE-V relocation effect modeling (the verifier currently sees
  the unresolved local offsets).
- Multiple program-type profiles (only `mvdp` is supported today).

These are tracked in subsequent prototype todos.

## Build

```bash
sudo apt install libelf-dev
make            # builds merlin-verifier
make test       # build the verifier and the synthetic-fixture
                # generator, then run a 10-case test battery
```

## Test architecture

We do not have a RISC-V cross-toolchain on the dev host, so we
cannot use real `gcc -march=rv64imac` to produce fixtures.  The
[`bad-progs/`](bad-progs/) directory contains:

- `mkfixture` — a small libelf-based helper that writes a minimal
  MERLIN-V ELF with one `.text.merlin.mvdp.test` section whose
  contents are user-supplied 32-bit hex literals (i.e. hand-encoded
  RV64 instructions).
- `run.sh` — drives `mkfixture` through ten positive and negative
  cases, asserts the verifier's verdict, and prints PASS/FAIL.

Cases include:

| Case | Expectation |
| ---- | ----------- |
| `helper_redirect`     | accept: `addi a7,x0,0x201; ecall; ret`            |
| `drop_only`           | accept: `addi a0,x0,1; ret`                       |
| `two_helpers`         | accept: two helper calls in sequence              |
| `ebreak`              | reject: `ebreak` is forbidden                     |
| `csrrw`               | reject: CSR write is forbidden                    |
| `ecall_bare`          | reject: `ecall` with no preceding constant a7     |
| `ecall_disallowed`    | reject: a7 = 0x301 not in MVDP allowlist          |
| `load_unknown`        | reject: load through unknown register             |
| `back_edge`           | reject: `beq x0,x0,-4`                            |
| `ecall_a7_nonconst`   | reject: a7 produced by ALU, not constant load     |

The test battery is the executable spec for what v0 accepts and
rejects.  Future work that loosens or tightens rules updates the
battery in lock-step.

## Verbose tracing

```
$ ./merlin-verifier -v bad-progs/case_two_helpers.o
  000000b4  11000893  alu-imm
  000000b8  00000073  ecall
    helper call: id 0x110  OK
  000000bc  20100893  alu-imm
  000000c0  00000073  ecall
    helper call: id 0x201  OK
  000000c4  00008067  jalr
ACCEPT  .text.merlin.mvdp.test  (20 bytes)  insns=5 helper-calls=2 jmp=1 ...
```

## Relationship to other tools

- `merlin-objtool` is the static-validation front-end: it ensures
  the ELF layout is right.  `merlin-verifier` then ensures the
  bytecode is right.  In production the two run as one pipeline:
  `merlin-objtool validate && merlin-verifier`.
- `proto-kernel-loader` (next prototype) will incorporate this
  verifier as a kernel-space module; this user-space tool stays as
  the iteration vehicle.
