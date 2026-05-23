// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * lab-03/src/decode.c — RV32I instruction decoder.
 *
 * PROVIDED — do not modify.
 *
 * This is a reference decoder that students consume.  It does not
 * implement decoding for the C extension, FP, AMO, or privileged
 * instructions.  Unknown encodings are returned as C_INVALID.
 */

#include "interp.h"
#include "rv32.h"

#include <string.h>

static int32_t imm_i(uint32_t i) { return (int32_t)i >> 20; }
static int32_t imm_s(uint32_t i)
{
	uint32_t hi = rv_bits(i, 31, 25), lo = rv_bits(i, 11, 7);
	return rv_sext((hi << 5) | lo, 12);
}
static int32_t imm_b(uint32_t i)
{
	uint32_t r = (rv_bits(i,31,31) << 12) | (rv_bits(i,7,7) << 11) |
		     (rv_bits(i,30,25) << 5)  | (rv_bits(i,11,8) << 1);
	return rv_sext(r, 13);
}
static int32_t imm_u(uint32_t i)  { return (int32_t)(i & 0xfffff000u); }
static int32_t imm_j(uint32_t i)
{
	uint32_t r = (rv_bits(i,31,31) << 20) | (rv_bits(i,19,12) << 12) |
		     (rv_bits(i,20,20) << 11) | (rv_bits(i,30,21) << 1);
	return rv_sext(r, 21);
}
static int32_t imm_shamt(uint32_t i) { return (int32_t)((i >> 20) & 0x1f); }

int rv_decode(uint32_t w, uint32_t pc, struct rv_insn *out)
{
	uint32_t op = w & 0x7f;
	uint32_t f3 = rv_bits(w, 14, 12);
	uint32_t f7 = rv_bits(w, 31, 25);

	memset(out, 0, sizeof(*out));
	out->raw = w;
	out->pc  = pc;
	out->rd  = rv_bits(w, 11, 7);
	out->rs1 = rv_bits(w, 19, 15);
	out->rs2 = rv_bits(w, 24, 20);
	out->funct3 = f3;

	if ((w & 3) != 3) { out->cls = C_INVALID; return -1; }

	switch (op) {
	case RV_OP_LUI:    out->cls = C_LUI;    out->imm = imm_u(w); return 0;
	case RV_OP_AUIPC:  out->cls = C_AUIPC;  out->imm = imm_u(w); return 0;
	case RV_OP_JAL:    out->cls = C_JAL;    out->imm = imm_j(w); return 0;
	case RV_OP_JALR:   out->cls = C_JALR;   out->imm = imm_i(w); return 0;
	case RV_OP_BRANCH: out->cls = C_BRANCH; out->imm = imm_b(w); return 0;
	case RV_OP_LOAD:   out->cls = C_LOAD;   out->imm = imm_i(w); return 0;
	case RV_OP_STORE:  out->cls = C_STORE;  out->imm = imm_s(w); return 0;

	case RV_OP_ALUI:
		switch (f3) {
		case 0: out->cls = C_ALU_IMM;   out->alu_op = OP_ADD;
			out->imm = imm_i(w); return 0;
		case 1: out->cls = C_SHIFT_IMM; out->alu_op = OP_SLL;
			out->imm = imm_shamt(w); return 0;
		case 2: out->cls = C_ALU_IMM;   out->alu_op = OP_SLT;
			out->imm = imm_i(w); return 0;
		case 3: out->cls = C_ALU_IMM;   out->alu_op = OP_SLTU;
			out->imm = imm_i(w); return 0;
		case 4: out->cls = C_ALU_IMM;   out->alu_op = OP_XOR;
			out->imm = imm_i(w); return 0;
		case 5: out->cls = C_SHIFT_IMM;
			out->alu_op = (f7 & 0x20) ? OP_SRA : OP_SRL;
			out->imm = imm_shamt(w); return 0;
		case 6: out->cls = C_ALU_IMM;   out->alu_op = OP_OR;
			out->imm = imm_i(w); return 0;
		case 7: out->cls = C_ALU_IMM;   out->alu_op = OP_AND;
			out->imm = imm_i(w); return 0;
		}
		break;

	case RV_OP_ALUR: {
		static const enum rv_alu_op map[8] = {
			OP_ADD, OP_SLL, OP_SLT, OP_SLTU,
			OP_XOR, OP_SRL, OP_OR,  OP_AND,
		};
		out->cls = C_ALU_REG;
		if (f7 == 0x20 && (f3 == 0 || f3 == 5))
			out->alu_op = (f3 == 0) ? OP_SUB : OP_SRA;
		else if (f7 == 0)
			out->alu_op = map[f3];
		else
			{ out->cls = C_INVALID; return -1; }
		return 0;
	}

	case RV_OP_FENCE:  out->cls = C_FENCE; return 0;
	case RV_OP_SYSTEM:
		if (f3 == 0 && out->rd == 0 && out->rs1 == 0) {
			uint32_t imm12 = w >> 20;
			if (imm12 == 0) { out->cls = C_ECALL;  return 0; }
			if (imm12 == 1) { out->cls = C_EBREAK; return 0; }
		}
		break;
	}

	out->cls = C_INVALID;
	return -1;
}
