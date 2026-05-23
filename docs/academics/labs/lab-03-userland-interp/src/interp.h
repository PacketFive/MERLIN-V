/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * lab-03/src/interp.h — RV32I user-space interpreter.
 *
 * PROVIDED — do not modify.
 *
 * The interpreter operates over the decoded instruction stream
 * provided by decode.c.  You implement the per-class execution
 * functions in interp.c.
 */
#ifndef LAB_INTERP_H
#define LAB_INTERP_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Decoded instruction (decode.c fills this). */
enum rv_class {
	C_INVALID = 0,
	C_LUI, C_AUIPC,
	C_JAL, C_JALR,
	C_BRANCH,
	C_LOAD, C_STORE,
	C_ALU_IMM, C_SHIFT_IMM, C_ALU_REG,
	C_FENCE, C_ECALL, C_EBREAK,
};

enum rv_alu_op {
	OP_ADD, OP_SUB, OP_SLL, OP_SLT, OP_SLTU,
	OP_XOR, OP_SRL, OP_SRA, OP_OR,  OP_AND,
};

struct rv_insn {
	uint32_t     raw;
	uint32_t     pc;
	enum rv_class cls;
	uint8_t      rd, rs1, rs2;
	int32_t      imm;
	enum rv_alu_op alu_op;
	uint8_t      funct3;     /* used by LOAD/STORE/BRANCH */
};

int rv_decode(uint32_t insn, uint32_t pc, struct rv_insn *out);

/* Interpreter state and run path. */
#define INTERP_STACK_BYTES  4096
#define INTERP_MAX_STEPS    (1u << 20)   /* termination cap */

struct interp_state {
	uint32_t x[32];                     /* GPRs                       */
	uint32_t pc;                        /* program counter            */
	bool     trace;                     /* dump each insn to stderr   */
	bool     halted;                    /* set by jalr x0, ra, 0      */
	uint32_t exit_value;                /* a0 at halt                 */
	uint8_t  stack[INTERP_STACK_BYTES]; /* program stack              */
	uint32_t steps;
	/* Read-only program text */
	const uint8_t *text;
	size_t         text_size;
	uint32_t       text_addr;           /* base addr for jumps        */
};

void interp_init(struct interp_state *s,
		 const uint8_t *text, size_t text_size, uint32_t text_addr,
		 uint32_t initial_a0);

/* Execute up to INTERP_MAX_STEPS instructions or until halted.
 * Returns 0 on normal halt; -1 on fault (out-of-bounds fetch,
 * undefined instruction, step cap exceeded).
 */
int interp_run(struct interp_state *s);

#endif /* LAB_INTERP_H */
