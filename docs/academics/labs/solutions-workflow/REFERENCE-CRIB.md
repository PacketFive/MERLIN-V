# Reference Implementation Crib

> **Audience.** Instructor setting up the private solutions repo.
>
> This crib gives the *minimum implementation* of every TODO block
> across labs 02-04.  Copy-paste into the corresponding TODO slot
> on the private solutions mirror; the autograder should then
> report 100/100.

---

## Lab 02 — `exercises.c`

Each `check(name, TODO_ENCODING, expected);` line: replace
`TODO_ENCODING` with the expected value.

```c
check("E1 addi a0, zero, 1",       0x00100513, 0x00100513);
check("E2 addi a0, zero, -1",      0xfff00513, 0xfff00513);
check("E3 add a0, a0, a1",         0x00b50533, 0x00b50533);
check("E4 lui a0, 0x12345",        0x12345537, 0x12345537);
check("E5 jalr zero, ra, 0",       0x00008067, 0x00008067);
check("E6 ecall",                  0x00000073, 0x00000073);
```

---

## Lab 03 — `src/interp.c`

### TODO #1: `execute_alu_imm`

```c
static void execute_alu_imm(struct interp_state *s, const struct rv_insn *in)
{
    uint32_t a   = s->x[in->rs1];
    int32_t  imm = in->imm;
    uint32_t r;
    switch (in->alu_op) {
    case OP_ADD:  r = a + (uint32_t)imm;                            break;
    case OP_AND:  r = a & (uint32_t)imm;                            break;
    case OP_OR:   r = a | (uint32_t)imm;                            break;
    case OP_XOR:  r = a ^ (uint32_t)imm;                            break;
    case OP_SLT:  r = ((int32_t)a < imm) ? 1 : 0;                   break;
    case OP_SLTU: r = (a < (uint32_t)imm) ? 1 : 0;                  break;
    default:      r = 0;                                            break;
    }
    if (in->rd != 0) s->x[in->rd] = r;
    s->pc += 4;
}
```

### TODO #2: `execute_load`

```c
static void execute_load(struct interp_state *s, const struct rv_insn *in)
{
    uint32_t addr = s->x[in->rs1] + (uint32_t)in->imm;
    uint32_t r = 0;
    switch (in->funct3) {
    case 0: r = (int32_t)(int8_t) *(int8_t  *)(uintptr_t)addr; break;  /* LB  */
    case 1: r = (int32_t)(int16_t)*(int16_t *)(uintptr_t)addr; break;  /* LH  */
    case 2: r =                   *(uint32_t*)(uintptr_t)addr; break;  /* LW  */
    case 4: r =                   *(uint8_t *)(uintptr_t)addr; break;  /* LBU */
    case 5: r =                   *(uint16_t*)(uintptr_t)addr; break;  /* LHU */
    default:
        s->halted = true; s->exit_value = (uint32_t)-1; return;
    }
    if (in->rd != 0) s->x[in->rd] = r;
    s->pc += 4;
}
```

### TODO #3: `execute_store`

```c
static void execute_store(struct interp_state *s, const struct rv_insn *in)
{
    uint32_t addr = s->x[in->rs1] + (uint32_t)in->imm;
    uint32_t v    = s->x[in->rs2];
    switch (in->funct3) {
    case 0: *(uint8_t  *)(uintptr_t)addr = (uint8_t)v;  break;  /* SB */
    case 1: *(uint16_t *)(uintptr_t)addr = (uint16_t)v; break;  /* SH */
    case 2: *(uint32_t *)(uintptr_t)addr = v;            break;  /* SW */
    default:
        s->halted = true; s->exit_value = (uint32_t)-1; return;
    }
    s->pc += 4;
}
```

### TODO #4: `execute_branch`

```c
static void execute_branch(struct interp_state *s, const struct rv_insn *in)
{
    uint32_t a = s->x[in->rs1], b = s->x[in->rs2];
    bool taken;
    switch (in->funct3) {
    case 0: taken = (a == b);                            break;  /* BEQ  */
    case 1: taken = (a != b);                            break;  /* BNE  */
    case 4: taken = ((int32_t)a <  (int32_t)b);          break;  /* BLT  */
    case 5: taken = ((int32_t)a >= (int32_t)b);          break;  /* BGE  */
    case 6: taken = (a <  b);                            break;  /* BLTU */
    case 7: taken = (a >= b);                            break;  /* BGEU */
    default:
        s->halted = true; s->exit_value = (uint32_t)-1; return;
    }
    s->pc += taken ? (uint32_t)in->imm : 4;
}
```

### TODO #5: `execute_ecall`

```c
static void execute_ecall(struct interp_state *s, const struct rv_insn *in)
{
    (void)in;
    uint32_t helper_id = s->x[RV_A7];
    s->x[RV_A0] = do_helper(s, helper_id);
    s->pc += 4;
}
```

After all five TODOs:

```
$ make test
  [PASS] 01-addi          -> 42
  [PASS] 02-loop-bounded  -> 10
  [PASS] 03-sum-args      -> 42
  [PASS] 04-lui-immediate -> 305419896
  [PASS] 05-shifts        -> 32
  [PASS] 06-branch-not-taken -> 99
  [PASS] 07-branch-taken  -> 42

7/7 tests passed.
```

---

## Lab 04 — `src/verify.c`

Inside the per-instruction `for` loop, after the existing
`rv_decode` call.  Replace the five `TODO #N` blocks:

### TODO #1: profile check

```c
if (in.cls != C_LUI && in.cls != C_AUIPC &&
    in.cls != C_JAL && in.cls != C_JALR &&
    in.cls != C_BRANCH && in.cls != C_LOAD && in.cls != C_STORE &&
    in.cls != C_ALU_IMM && in.cls != C_SHIFT_IMM && in.cls != C_ALU_REG &&
    in.cls != C_FENCE && in.cls != C_ECALL) {
    REJECT("forbidden class %d", (int)in.cls);
    continue;
}
```

### TODO #2: back-edge check

```c
if ((in.cls == C_JAL || in.cls == C_BRANCH) &&
    in.imm < 0 && !cfg->allow_back_edges) {
    REJECT("back-edge imm=%d", in.imm);
}
```

### TODO #3: ecall ABI

```c
if (in.cls == C_ECALL) {
    if (x[RV_A7].kind != RVAL_CONST) {
        REJECT("ecall non-const a7");
    } else if (x[RV_A7].val > MAX_HELPER_ID ||
               !((1u << x[RV_A7].val) & cfg->helper_allow)) {
        REJECT("helper id %u not allowed", x[RV_A7].val);
    } else {
        x[RV_A0] = (struct rval){
            .kind = RVAL_PTR_HELPER_RET, .val = x[RV_A7].val
        };
    }
    /* clobber caller-saved a1..a7, t0..t6 */
    for (int r = 11; r <= 17; r++) x[r].kind = RVAL_UNKNOWN;
    for (int r = 5;  r <= 7;  r++) x[r].kind = RVAL_UNKNOWN;
    for (int r = 28; r <= 31; r++) x[r].kind = RVAL_UNKNOWN;
    continue;
}
```

### TODO #4: load/store address check

```c
if (in.cls == C_LOAD || in.cls == C_STORE) {
    enum rval_kind k = x[in.rs1].kind;
    if (k == RVAL_UNKNOWN || k == RVAL_CONST) {
        REJECT("%s thru x%u kind=%d",
               in.cls == C_LOAD ? "load" : "store", in.rs1, (int)k);
    }
    if (in.cls == C_LOAD && in.rd != 0)
        x[in.rd] = (struct rval){ .kind = RVAL_UNKNOWN };
    continue;
}
```

### TODO #5: ALU effects

```c
if (in.cls == C_ALU_IMM && in.alu_op == OP_ADD) {
    if (in.rs1 == 0) {
        x[in.rd] = (struct rval){ .kind = RVAL_CONST, .val = (uint32_t)in.imm };
    } else if (x[in.rs1].kind == RVAL_CONST) {
        x[in.rd] = (struct rval){
            .kind = RVAL_CONST, .val = x[in.rs1].val + (uint32_t)in.imm
        };
    } else if (x[in.rs1].kind == RVAL_PTR_CTX ||
               x[in.rs1].kind == RVAL_PTR_STACK) {
        x[in.rd] = (struct rval){
            .kind = x[in.rs1].kind,
            .val  = x[in.rs1].val + (uint32_t)in.imm,
        };
    } else if (in.rd != 0) {
        x[in.rd] = (struct rval){ .kind = RVAL_UNKNOWN };
    }
    continue;
}

if (in.cls == C_LUI) {
    if (in.rd != 0)
        x[in.rd] = (struct rval){ .kind = RVAL_CONST, .val = (uint32_t)in.imm };
    continue;
}

if (in.cls == C_AUIPC) {
    if (in.rd != 0) x[in.rd] = (struct rval){ .kind = RVAL_UNKNOWN };
    continue;
}

/* Default: clobber rd */
if (in.rd != 0) x[in.rd] = (struct rval){ .kind = RVAL_UNKNOWN };
```

After all five TODOs:

```
$ make test
  [PASS] accept/01-trivial.bin
  [PASS] accept/02-const.bin
  [PASS] accept/03-ctx-load.bin
  [PASS] accept/04-helper-call.bin
  [PASS] accept/05-fwd-branch.bin
  [PASS] reject/01-bad-load.bin
  [PASS] reject/02-back-edge.bin
  [PASS] reject/03-ecall-nonconst.bin
  [PASS] reject/04-ecall-bad-id.bin
  [PASS] reject/05-forbidden-csr.bin
  [PASS] reject/06-bad-store.bin

11/11 tests passed.
```

---

## Verifying on a fresh clone

```bash
git clone git@github.com:PacketFive/MERLIN-V-solutions.git
cd MERLIN-V-solutions
docs/academics/labs/autograder/runner.sh lab-02 | \
    docs/academics/labs/autograder/score.py lab-02
docs/academics/labs/autograder/runner.sh lab-03 | \
    docs/academics/labs/autograder/score.py lab-03
docs/academics/labs/autograder/runner.sh lab-04 | \
    docs/academics/labs/autograder/score.py lab-04
```

All three should print `Score: 100  Grade: A`.

## Assisted-by

Copilot-CLI:Claude-Opus
