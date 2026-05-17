# `merlin-jit-x86_64` — prototype host JIT

The third user-space code prototype of MERLIN-V.  A working
single-pass RV64 → x86_64 translator that produces real executable
host code from a verified MERLIN-V `.text.merlin.*` section.

This is the user-space embodiment of the "host JIT path" in
[`../../docs/design/07-jit-and-offload.md`](../../docs/design/07-jit-and-offload.md) §3.
The same algorithm will eventually live under
`kernel/merlin/jit/arch/x86_64.c` in the in-tree work; this
prototype is the iteration vehicle.

## What it does

1. Parses the ELF and finds the first `.text.merlin.*` section.
2. Translates each verified RV64 instruction into a small x86_64
   sequence using the spill-everything register strategy:
   - All 32 RV registers live in a uint64_t array on the JIT'd
     function's stack frame.
   - `rbx` is pinned to point at `&regs[0]` for the function's
     lifetime.
   - Each RV instruction becomes "load operands from regs[],
     compute, store back" — 4-8 x86 instructions per RV
     instruction.
3. `mmap`s a `PROT_READ | PROT_EXEC` page, copies the host bytes,
   and casts to `uint64_t (*)(void *ctx)`.
4. Calls the function with a stub ctx and prints the return value
   plus a histogram of helper calls.

## What's supported (v0)

| RV instruction class | Status |
| -------------------- | ------ |
| `addi rd, rs1, imm`  | ✅ (only `add`; other ALU ops via this opcode are todo) |
| `lui rd, imm`        | ✅ |
| `add/sub/or/and/xor` (reg-reg) | ✅ |
| `mul`                | ✅ |
| `slli/srli/srai`     | ✅ |
| `ecall`              | ✅ — translates to call into a C trampoline that reads `a7` / `a0..a5` from regs[], dispatches via helper id, writes the return back to a0 |
| `jalr x0, ra, 0` (= `ret`) | ✅ — translates to the function epilogue |
| `fence`              | ✅ — treated as a no-op on x86 (which dominates RVWMO for the atomics MERLIN-V supports today) |

## What's not yet implemented

These are intentional Phase 2 work:

- All other `addi`-variants (slti/sltiu/xori/ori/andi).
- Loads and stores (lb/lh/lw/ld/lbu/lhu/lwu and the four store kinds).
  The decoder recognises them; the translator emits "instruction
  class not yet supported".
- Branches (the test fixtures use straight-line code).
- `jal` (non-`ret` form).
- RV64I word-form arithmetic (`addw`, `subw`, `sllw`, `srlw`, `sraw`).
- Division / remainder (the M-extension dispatch hits the "not
  supported" path).
- Real register allocation (current strategy: zero allocation;
  every RV reg lives in memory).
- CO-RE-V relocation effects on operand addresses.

The set above is large enough to JIT every positive-test fixture
the verifier accepts (the three accept-cases plus a handful of
arithmetic patterns).  Each missing instruction is a small,
mechanical addition: write one emit helper, one translate case.

## Build

```bash
sudo apt install libelf-dev
make             # builds merlin-jit (links the verifier's decoder)
make test        # builds 6 fixtures and runs them, asserting
                 # exact return values + helper-call counts
```

## Test architecture

The JIT test battery reuses `mkfixture` from
[`../merlin-verifier/bad-progs/`](../merlin-verifier/bad-progs/)
to materialise hand-encoded RV64 instructions into real MERLIN-V
.o files.  `run-tests.sh` then drives the JIT against each fixture
and asserts the return value and helper-call counters.

```
== merlin-jit end-to-end test battery ==
  [PASS] drop             (ret=0x1, helpers=[none])
  [PASS] ret255           (ret=0xff, helpers=[none])
  [PASS] ret0             (ret=0x0, helpers=[none])
  [PASS] helper_redirect  (ret=0x4edcab1e, helpers=[0x201])
  [PASS] ktime            (ret=0xcafe0000, helpers=[0x110])
  [PASS] two_helpers      (ret=0x4edcab1e, helpers=[0x110 0x201])
```

The stub helper trampoline (`helpers.c`) returns recognisable
sentinel values per helper ID so the test can prove the JIT'd
ecall lands at the right dispatch arm:

| Helper ID | Stub returns |
| --------- | ------------ |
| `0x0101` map_lookup_elem    | `0xDEADBEEF` |
| `0x0110` ktime_get_ns       | `0xCAFE0000` |
| `0x0120` get_prandom_u32    | `0xC001D00D` |
| `0x0201` mvdp_redirect      | `0x4EDCAB1E` |

Real helper bodies will land in `kernel/merlin/helpers.c` when the
in-tree work begins; the trampoline interface is stable already.

## Disassembly

`./merlin-jit -d <object.o>` dumps the emitted x86_64 hex.
Annotated example for `two_helpers`:

```
   prologue
00: 53                              push rbx
01: 48 81 ec 00 01 00 00            sub  rsp, 256
08: 48 89 e3                        mov  rbx, rsp
0b: 48 31 c0                        xor  rax, rax
0e: 48 89 83 00 00 00 00            mov  [rbx+0], rax        regs[0] = 0
... 31 more zeroing stores ...
ee: 48 89 bb 50 00 00 00            mov  [rbx+80], rdi       regs[10]=ctx

   addi a7, x0, 0x110
f5: 48 8b 83 00 00 00 00            mov  rax, [rbx+0]
fc: 48 05 10 01 00 00               add  rax, 0x110
102: 48 89 83 88 00 00 00           mov  [rbx+136], rax      regs[17]=0x110

   ecall
109: 48 89 df                       mov  rdi, rbx
10c: 48 b8 <8 bytes>                mov  rax, &trampoline
116: ff d0                          call rax

   (the addi/ecall pair repeats for 0x201)

   jalr x0, ra, 0  (epilogue)
13c: 48 8b 83 50 00 00 00           mov  rax, [rbx+80]
143: 48 81 c4 00 01 00 00           add  rsp, 256
14a: 5b                             pop  rbx
14b: c3                             ret
```

## Relationship to the rest of the toolchain

```
   .merlin.o
       |  parse + validate
       v
   merlin-objtool                       [protoype-1]
       |  hand off accepted ELF
       v
   merlin-verifier                      [prototype-2]
       |  hand off verified bytecode
       v
   merlin-jit                           [prototype-3, this directory]
       |  mmap PROT_EXEC, call
       v
   x86_64 native execution
```

The chain is correct end-to-end on dev hosts today.  The in-kernel
work (`proto-kernel-loader`) will reproduce this pipeline inside
`kernel/merlin/` with the same algorithms.
