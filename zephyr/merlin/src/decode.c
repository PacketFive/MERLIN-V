/* SPDX-License-Identifier: Apache-2.0 */
/*
 * decode.c — RV32 instruction decoder for the MERLIN-V Zephyr verifier.
 *
 * Cut-down kernel port: same algorithm as kernel/merlin/decode.c, but
 * - RV32-only (RTOS profile is rv32imc; RV64 width bits unused),
 * - 32-bit imm field (fits the 12-bit/20-bit immediates),
 * - C extension (16-bit) still flagged UNSUPPORTED for Phase 1,
 * - smaller name table to save flash.
 *
 * Reference: RISC-V Unprivileged ISA v20191213.
 * See docs/design/02-isa-and-bytecode.md §§3-4.
 */

#include "merlin_internal.h"
#include <string.h>

static inline uint32_t bits_(uint32_t v, int hi, int lo)
{
	return (v >> lo) & ((1u << (hi - lo + 1)) - 1);
}

static inline int32_t sext(uint32_t v, int width)
{
	int shift = 32 - width;
	return (int32_t)(v << shift) >> shift;
}

static inline int32_t imm_i_(uint32_t i) { return (int32_t)i >> 20; }
static inline int32_t imm_s_(uint32_t i)
{
	uint32_t hi = bits_(i, 31, 25), lo = bits_(i, 11, 7);
	return sext((hi << 5) | lo, 12);
}
static inline int32_t imm_b_(uint32_t i)
{
	uint32_t r = (bits_(i,31,31) << 12) | (bits_(i,7,7) << 11) |
		     (bits_(i,30,25) << 5)  | (bits_(i,11,8) << 1);
	return sext(r, 13);
}
static inline int32_t imm_u_(uint32_t i) { return (int32_t)(i & 0xfffff000u); }
static inline int32_t imm_j_(uint32_t i)
{
	uint32_t r = (bits_(i,31,31) << 20) | (bits_(i,19,12) << 12) |
		     (bits_(i,20,20) << 11) | (bits_(i,30,21) << 1);
	return sext(r, 21);
}
static inline int32_t imm_shamt_(uint32_t i) { return (int32_t)((i >> 20) & 0x1f); }

int merlin_decode(uint32_t w, uint32_t pc, struct merlin_insn *out)
{
	uint32_t opcode = w & 0x7f;
	uint32_t f3 = bits_(w, 14, 12);
	uint32_t f7 = bits_(w, 31, 25);

	memset(out, 0, sizeof(*out));
	out->raw = w;
	out->pc  = pc;

	if ((w & 3) != 3) {
		out->cls = INSN_UNSUPPORTED;  /* compressed: phase 2 */
		return 0;
	}

	out->rd  = bits_(w, 11, 7);
	out->rs1 = bits_(w, 19, 15);
	out->rs2 = bits_(w, 24, 20);

	switch (opcode) {
	case 0x37: out->cls = INSN_LUI;   out->imm = imm_u_(w); return 0;
	case 0x17: out->cls = INSN_AUIPC; out->imm = imm_u_(w); return 0;
	case 0x6f: out->cls = INSN_JAL;   out->imm = imm_j_(w); return 0;
	case 0x67:
		if (f3 != 0) { out->cls = INSN_INVALID; return -1; }
		out->cls = INSN_JALR; out->imm = imm_i_(w); return 0;

	case 0x63: out->cls = INSN_BRANCH; out->imm = imm_b_(w); return 0;
	case 0x03: out->cls = INSN_LOAD;   out->imm = imm_i_(w); return 0;
	case 0x23: out->cls = INSN_STORE;  out->imm = imm_s_(w); return 0;

	case 0x13: {  /* OP-IMM */
		switch (f3) {
		case 0: out->cls = INSN_ALU_IMM;   out->alu_op = ALU_ADD;
			out->imm = imm_i_(w); return 0;
		case 1: out->cls = INSN_SHIFT_IMM; out->alu_op = ALU_SLL;
			out->imm = imm_shamt_(w); return 0;
		case 2: out->cls = INSN_ALU_IMM;   out->alu_op = ALU_SLT;
			out->imm = imm_i_(w); return 0;
		case 3: out->cls = INSN_ALU_IMM;   out->alu_op = ALU_SLTU;
			out->imm = imm_i_(w); return 0;
		case 4: out->cls = INSN_ALU_IMM;   out->alu_op = ALU_XOR;
			out->imm = imm_i_(w); return 0;
		case 5: out->cls = INSN_SHIFT_IMM;
			out->alu_op = (f7 & 0x20) ? ALU_SRA : ALU_SRL;
			out->imm = imm_shamt_(w); return 0;
		case 6: out->cls = INSN_ALU_IMM;   out->alu_op = ALU_OR;
			out->imm = imm_i_(w); return 0;
		case 7: out->cls = INSN_ALU_IMM;   out->alu_op = ALU_AND;
			out->imm = imm_i_(w); return 0;
		}
		break;
	}

	case 0x33: {  /* OP */
		static const enum merlin_alu_op alu_ops[8] = {
			ALU_ADD, ALU_SLL, ALU_SLT, ALU_SLTU,
			ALU_XOR, ALU_SRL, ALU_OR,  ALU_AND,
		};
		if (f7 == 1) {
			if (f3 < 4) {
				static const enum merlin_alu_op m_ops[4] = {
					ALU_MUL, ALU_MULH, ALU_MULHSU, ALU_MULHU,
				};
				out->cls = INSN_MUL; out->alu_op = m_ops[f3];
			} else {
				static const enum merlin_alu_op d_ops[4] = {
					ALU_DIV, ALU_DIVU, ALU_REM, ALU_REMU,
				};
				out->cls = INSN_DIV; out->alu_op = d_ops[f3 - 4];
			}
			return 0;
		}
		out->cls = INSN_ALU_REG;
		if (f7 == 0x20 && (f3 == 0 || f3 == 5))
			out->alu_op = (f3 == 0) ? ALU_SUB : ALU_SRA;
		else if (f7 == 0)
			out->alu_op = alu_ops[f3];
		else { out->cls = INSN_INVALID; return -1; }
		return 0;
	}

	case 0x0f:  /* FENCE / FENCE.I */
		out->cls = (f3 == 1) ? INSN_FENCE_I : INSN_FENCE;
		return 0;

	case 0x73:  /* SYSTEM */
		if (f3 == 0 && out->rd == 0 && out->rs1 == 0) {
			uint32_t imm12 = w >> 20;
			if (imm12 == 0) { out->cls = INSN_ECALL;  return 0; }
			if (imm12 == 1) { out->cls = INSN_EBREAK; return 0; }
			out->cls = INSN_PRIV;
			return 0;
		}
		out->cls = INSN_CSR;
		return 0;

	case 0x2f:  /* AMO */
		out->cls = INSN_UNSUPPORTED;
		return 0;

	case 0x07: case 0x27:
	case 0x43: case 0x47: case 0x4b: case 0x4f: case 0x53:
		out->cls = INSN_FLOAT;
		return 0;
	}

	out->cls = INSN_INVALID;
	return -1;
}

bool merlin_insn_permitted_rtos(const struct merlin_insn *in)
{
	switch (in->cls) {
	case INSN_LUI: case INSN_AUIPC:
	case INSN_JAL: case INSN_JALR:
	case INSN_BRANCH:
	case INSN_LOAD: case INSN_STORE:
	case INSN_ALU_IMM: case INSN_SHIFT_IMM: case INSN_ALU_REG:
	case INSN_FENCE: case INSN_ECALL:
	case INSN_MUL: case INSN_DIV:
		return true;
	default:
		return false;
	}
}

const char *merlin_insn_class_name(enum merlin_insn_class cls)
{
	switch (cls) {
	case INSN_LUI:        return "LUI";
	case INSN_AUIPC:      return "AUIPC";
	case INSN_JAL:        return "JAL";
	case INSN_JALR:       return "JALR";
	case INSN_BRANCH:     return "BRANCH";
	case INSN_LOAD:       return "LOAD";
	case INSN_STORE:      return "STORE";
	case INSN_ALU_IMM:    return "ALU_IMM";
	case INSN_SHIFT_IMM:  return "SHIFT_IMM";
	case INSN_ALU_REG:    return "ALU_REG";
	case INSN_FENCE:      return "FENCE";
	case INSN_ECALL:      return "ECALL";
	case INSN_EBREAK:     return "EBREAK";
	case INSN_MUL:        return "MUL";
	case INSN_DIV:        return "DIV";
	case INSN_CSR:        return "CSR";
	case INSN_PRIV:       return "PRIV";
	case INSN_FENCE_I:    return "FENCE_I";
	case INSN_FLOAT:      return "FLOAT";
	case INSN_UNSUPPORTED:return "UNSUPPORTED";
	default:              return "?";
	}
}
