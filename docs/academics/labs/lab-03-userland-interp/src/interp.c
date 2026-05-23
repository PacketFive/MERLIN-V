// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * lab-03/src/interp.c — RV32I user-space interpreter (skeleton).
 *
 * PARTIALLY PROVIDED — students fill in the TODO blocks.
 *
 * What is provided:
 *  - The fetch/decode/dispatch loop.
 *  - Helper-call dispatch (the trace_print / get_time_ns / rand_u32
 *    handlers).
 *  - The simpler instruction handlers (LUI, AUIPC, ALU_REG, SHIFT_IMM,
 *    JAL, JALR) — read them as worked examples.
 *
 * What students implement:
 *  - execute_alu_imm
 *  - execute_load
 *  - execute_store
 *  - execute_branch
 *  - execute_ecall
 *
 * Read interp.h and decode.c end-to-end before starting.
 */

#define _POSIX_C_SOURCE 200809L

#include "interp.h"
#include "rv32.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* -----------------------------------------------------------------
 * State init
 * ----------------------------------------------------------------- */
void interp_init(struct interp_state *s,
		 const uint8_t *text, size_t text_size, uint32_t text_addr,
		 uint32_t initial_a0)
{
	memset(s, 0, sizeof(*s));
	s->text      = text;
	s->text_size = text_size;
	s->text_addr = text_addr;
	s->pc        = text_addr;
	/* Stack grows down; sp starts at top of frame, 16-byte aligned. */
	s->x[RV_SP]  = (uint32_t)(uintptr_t)(s->stack + INTERP_STACK_BYTES);
	s->x[RV_A0]  = initial_a0;
	s->x[RV_RA]  = 0;   /* sentinel return address; jalr to 0 halts  */
}

/* -----------------------------------------------------------------
 * Helper-call dispatch (PROVIDED)
 * ----------------------------------------------------------------- */
static uint32_t do_helper(struct interp_state *s, uint32_t helper_id)
	__attribute__((unused));
static uint32_t do_helper(struct interp_state *s, uint32_t helper_id)
{
	switch (helper_id) {
	case MERLIN_HELPER_TRACE_PRINT: {
		/* a0 is a pointer to a NUL-terminated string somewhere in
		 * the host's address space (test programs pass the
		 * pointer of a string in the host).  Validate it
		 * pessimistically.
		 */
		const char *str = (const char *)(uintptr_t)s->x[RV_A0];
		if (str)
			fputs(str, stderr);
		return 0;
	}
	case MERLIN_HELPER_GET_TIME_NS: {
		struct timespec ts;
		clock_gettime(CLOCK_MONOTONIC, &ts);
		return (uint32_t)ts.tv_nsec;
	}
	case MERLIN_HELPER_RAND_U32:
		return (uint32_t)rand();
	default:
		fprintf(stderr, "interp: unknown helper id %u\n", helper_id);
		return (uint32_t)-1;
	}
}

/* -----------------------------------------------------------------
 * Fetch (PROVIDED)
 * ----------------------------------------------------------------- */
static int fetch(struct interp_state *s, uint32_t pc, uint32_t *out)
{
	uint32_t off;
	if (pc < s->text_addr)
		return -1;
	off = pc - s->text_addr;
	if (off + 4 > s->text_size)
		return -1;
	*out = (uint32_t)s->text[off]
	     | ((uint32_t)s->text[off + 1] << 8)
	     | ((uint32_t)s->text[off + 2] << 16)
	     | ((uint32_t)s->text[off + 3] << 24);
	return 0;
}

/* -----------------------------------------------------------------
 * Per-class executors
 * ----------------------------------------------------------------- */

/* PROVIDED — worked example. */
static void execute_lui(struct interp_state *s, const struct rv_insn *in)
{
	if (in->rd) s->x[in->rd] = (uint32_t)in->imm;
	s->pc += 4;
}

/* PROVIDED — worked example. */
static void execute_auipc(struct interp_state *s, const struct rv_insn *in)
{
	if (in->rd) s->x[in->rd] = s->pc + (uint32_t)in->imm;
	s->pc += 4;
}

/* PROVIDED — worked example. */
static void execute_alu_reg(struct interp_state *s, const struct rv_insn *in)
{
	uint32_t a = s->x[in->rs1], b = s->x[in->rs2], r = 0;
	switch (in->alu_op) {
	case OP_ADD:  r = a + b; break;
	case OP_SUB:  r = a - b; break;
	case OP_SLL:  r = a << (b & 0x1f); break;
	case OP_SLT:  r = ((int32_t)a < (int32_t)b) ? 1 : 0; break;
	case OP_SLTU: r = (a < b) ? 1 : 0; break;
	case OP_XOR:  r = a ^ b; break;
	case OP_SRL:  r = a >> (b & 0x1f); break;
	case OP_SRA:  r = (uint32_t)((int32_t)a >> (b & 0x1f)); break;
	case OP_OR:   r = a | b; break;
	case OP_AND:  r = a & b; break;
	}
	if (in->rd) s->x[in->rd] = r;
	s->pc += 4;
}

/* PROVIDED — worked example. */
static void execute_shift_imm(struct interp_state *s, const struct rv_insn *in)
{
	uint32_t a = s->x[in->rs1], r = 0;
	uint32_t sh = (uint32_t)in->imm & 0x1f;
	switch (in->alu_op) {
	case OP_SLL: r = a << sh; break;
	case OP_SRL: r = a >> sh; break;
	case OP_SRA: r = (uint32_t)((int32_t)a >> sh); break;
	default:     r = 0; break;
	}
	if (in->rd) s->x[in->rd] = r;
	s->pc += 4;
}

/* PROVIDED — worked example.
 * JAL: rd = pc + 4 ; pc += imm
 */
static void execute_jal(struct interp_state *s, const struct rv_insn *in)
{
	uint32_t link = s->pc + 4;
	if (in->rd) s->x[in->rd] = link;
	s->pc = s->pc + (uint32_t)in->imm;
}

/* PROVIDED — worked example.
 * JALR: rd = pc + 4 ; pc = (rs1 + imm) & ~1
 * The standard return pattern is `jalr x0, ra, 0`, which sets pc =
 * x[ra] & ~1.  Lab convention: initial ra=0, so a jalr-to-0 halts
 * and the exit value is a0.
 */
static void execute_jalr(struct interp_state *s, const struct rv_insn *in)
{
	uint32_t link   = s->pc + 4;
	uint32_t target = (s->x[in->rs1] + (uint32_t)in->imm) & ~1u;
	if (in->rd) s->x[in->rd] = link;
	if (target == 0) {
		s->halted     = true;
		s->exit_value = s->x[RV_A0];
		return;
	}
	s->pc = target;
}

/* ---------------- TODO #1: execute_alu_imm ----------------
 *
 * Implement ADDI, ANDI, ORI, XORI, SLTI, SLTIU.
 *
 * Pseudo-code:
 *   a = s->x[in->rs1]
 *   imm = (uint32_t)in->imm        (already sign-extended by decode.c)
 *   r = <apply in->alu_op to a and imm>
 *   if rd != 0: s->x[in->rd] = r
 *   s->pc += 4
 *
 * Hint: SLTI uses *signed* comparison; SLTIU uses *unsigned*.
 */
static void execute_alu_imm(struct interp_state *s, const struct rv_insn *in)
{
	/* TODO #1 — implement ALU-immediate.
	 *
	 * Remove the next line once you implement this.
	 */
	(void)in; s->pc += 4;
	(void)s;
}

/* ---------------- TODO #2: execute_load ----------------
 *
 * funct3 selects LB/LH/LW/LBU/LHU.
 *
 *   addr = s->x[in->rs1] + (uint32_t)in->imm
 *
 * Read 1/2/4 bytes from `addr` in little-endian order.  For the
 * lab interpreter, addresses point into the host process space
 * (the test program passes you pointers it expects to dereference);
 * cast (uintptr_t)addr to a pointer and read it.  Sign-extend or
 * zero-extend to 32 bits per funct3.
 *
 *   funct3=0 LB    sign-ext byte
 *   funct3=1 LH    sign-ext halfword
 *   funct3=2 LW    32-bit word
 *   funct3=4 LBU   zero-ext byte
 *   funct3=5 LHU   zero-ext halfword
 *
 * Other funct3 values → fault: set s->halted = true and
 *   s->exit_value = (uint32_t)-1 (autograder treats -1 as a fault).
 */
static void execute_load(struct interp_state *s, const struct rv_insn *in)
{
	/* TODO #2 */
	(void)in; s->pc += 4;
	(void)s;
}

/* ---------------- TODO #3: execute_store ----------------
 *
 *   addr = s->x[in->rs1] + (uint32_t)in->imm
 *   value = s->x[in->rs2]
 *   funct3=0 SB    store low 8 bits
 *   funct3=1 SH    store low 16 bits
 *   funct3=2 SW    store 32 bits
 */
static void execute_store(struct interp_state *s, const struct rv_insn *in)
{
	/* TODO #3 */
	(void)in; s->pc += 4;
	(void)s;
}

/* ---------------- TODO #4: execute_branch ----------------
 *
 * funct3:
 *   0 BEQ  rs1 == rs2
 *   1 BNE  rs1 != rs2
 *   4 BLT  signed   rs1 <  rs2
 *   5 BGE  signed   rs1 >= rs2
 *   6 BLTU unsigned rs1 <  rs2
 *   7 BGEU unsigned rs1 >= rs2
 *
 * If branch taken:    s->pc += (uint32_t)in->imm
 * If branch not taken: s->pc += 4
 *
 * Other funct3 → fault.
 */
static void execute_branch(struct interp_state *s, const struct rv_insn *in)
{
	/* TODO #4 */
	(void)in; s->pc += 4;
	(void)s;
}

/* ---------------- TODO #5: execute_ecall ----------------
 *
 *   helper_id = s->x[RV_A7]
 *   ret = do_helper(s, helper_id)
 *   s->x[RV_A0] = ret
 *   s->pc += 4
 */
static void execute_ecall(struct interp_state *s, const struct rv_insn *in)
{
	/* TODO #5 */
	(void)in; s->pc += 4;
	(void)s;
}

static void execute_fence(struct interp_state *s, const struct rv_insn *in)
{
	(void)in;
	s->pc += 4;  /* no-op for the user-space interpreter */
}

/* -----------------------------------------------------------------
 * Main dispatch loop (PROVIDED)
 * ----------------------------------------------------------------- */
int interp_run(struct interp_state *s)
{
	while (!s->halted && s->steps < INTERP_MAX_STEPS) {
		uint32_t w;
		struct rv_insn in;

		if (fetch(s, s->pc, &w) < 0) {
			fprintf(stderr, "interp: fetch fault at pc=0x%08x\n",
				s->pc);
			return -1;
		}
		if (rv_decode(w, s->pc, &in) < 0) {
			fprintf(stderr,
				"interp: undecodable insn 0x%08x at pc=0x%08x\n",
				w, s->pc);
			return -1;
		}
		if (s->trace)
			fprintf(stderr,
				"pc=%08x raw=%08x cls=%d rd=x%u rs1=x%u "
				"rs2=x%u imm=%d\n",
				s->pc, w, in.cls, in.rd, in.rs1, in.rs2, in.imm);

		/* x0 is always zero — clear it after every insn to enforce. */
		s->x[0] = 0;

		switch (in.cls) {
		case C_LUI:       execute_lui(s, &in);       break;
		case C_AUIPC:     execute_auipc(s, &in);     break;
		case C_JAL:       execute_jal(s, &in);       break;
		case C_JALR:      execute_jalr(s, &in);      break;
		case C_BRANCH:    execute_branch(s, &in);    break;
		case C_LOAD:      execute_load(s, &in);      break;
		case C_STORE:     execute_store(s, &in);     break;
		case C_ALU_IMM:   execute_alu_imm(s, &in);   break;
		case C_SHIFT_IMM: execute_shift_imm(s, &in); break;
		case C_ALU_REG:   execute_alu_reg(s, &in);   break;
		case C_FENCE:     execute_fence(s, &in);     break;
		case C_ECALL:     execute_ecall(s, &in);     break;
		default:
			fprintf(stderr, "interp: class %d unsupported "
				"(pc=0x%08x)\n", in.cls, s->pc);
			return -1;
		}
		s->x[0] = 0;
		s->steps++;
	}
	if (!s->halted) {
		fprintf(stderr, "interp: step cap (%u) exceeded\n",
			INTERP_MAX_STEPS);
		return -1;
	}
	return 0;
}
