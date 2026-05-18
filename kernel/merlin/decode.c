// SPDX-License-Identifier: GPL-2.0-only
/*
 * decode.c — RV32/RV64 instruction decoder for the MERLIN-V kernel verifier.
 *
 * This is a direct kernel-C port of tools/merlin-verifier/decode.c.
 * The algorithm and coverage are identical; the only changes are:
 *
 *   - libc types replaced by kernel types (uint32_t -> u32, etc.)
 *   - stdio removed; no printf.
 *   - stdbool replaced by <linux/types.h> bool.
 *   - EXPORT_SYMBOL_GPL on the public API used by verifier.c.
 *
 * Reference: RISC-V Unprivileged ISA v20191213, "Volume I".
 * See also docs/design/02-isa-and-bytecode.md §§3-4.
 */

#include <linux/types.h>
#include <linux/module.h>
#include <linux/string.h>

#include "include/merlin_internal.h"

/* -----------------------------------------------------------------------
 * Field extraction helpers
 * ----------------------------------------------------------------------- */

static inline u32 bits(u32 v, int hi, int lo)
{
	return (v >> lo) & ((1u << (hi - lo + 1)) - 1);
}

static inline s32 sign_ext(u32 v, int width)
{
	int shift = 32 - width;
	return (s32)((s32)(v << shift) >> shift);
}

/* RV32I/RV64I immediate formats */
static inline s64 imm_i(u32 insn)
{
	return (s64)(s32)(insn >> 20);          /* sign-extended 12-bit      */
}

static inline s64 imm_s(u32 insn)
{
	u32 hi = bits(insn, 31, 25);
	u32 lo = bits(insn, 11, 7);
	return (s64)sign_ext((hi << 5) | lo, 12);
}

static inline s64 imm_b(u32 insn)
{
	u32 b12  = bits(insn, 31, 31);
	u32 b11  = bits(insn, 7, 7);
	u32 b10_5 = bits(insn, 30, 25);
	u32 b4_1  = bits(insn, 11, 8);
	u32 raw  = (b12 << 12) | (b11 << 11) | (b10_5 << 5) | (b4_1 << 1);
	return (s64)sign_ext(raw, 13);
}

static inline s64 imm_u(u32 insn)
{
	return (s64)(s32)(insn & 0xfffff000u);  /* upper-20 already shifted  */
}

static inline s64 imm_j(u32 insn)
{
	u32 b20    = bits(insn, 31, 31);
	u32 b10_1  = bits(insn, 30, 21);
	u32 b11    = bits(insn, 20, 20);
	u32 b19_12 = bits(insn, 19, 12);
	u32 raw    = (b20 << 20) | (b19_12 << 12) | (b11 << 11) | (b10_1 << 1);
	return (s64)sign_ext(raw, 21);
}

static inline s64 imm_shamt(u32 insn, bool rv64)
{
	/* RV64: 6-bit shamt in bits [25:20]; RV32: 5-bit in bits [24:20] */
	u32 mask = rv64 ? 0x3f : 0x1f;
	return (s64)((insn >> 20) & mask);
}

/* -----------------------------------------------------------------------
 * Opcode constants (used below for clarity)
 * ----------------------------------------------------------------------- */
#define OP_LOAD    0x03
#define OP_FENCE   0x0f
#define OP_ALUI    0x13
#define OP_AUIPC   0x17
#define OP_STORE   0x23
#define OP_AMO     0x2f
#define OP_ALUR    0x33
#define OP_LUI     0x37
#define OP_BRANCH  0x63
#define OP_JALR    0x67
#define OP_JAL     0x6f
#define OP_SYSTEM  0x73
#define OP_ALUW    0x3b   /* RV64I *W ops (addw, subw, ...) */
#define OP_ALUIW   0x1b   /* RV64I ALUI *W (addiw, slliw, ...) */
#define OP_FLOAT   0x07   /* load FP */
#define OP_STORE_FP 0x27  /* store FP */
#define OP_FMADD   0x43
#define OP_FMSUB   0x47
#define OP_FNMSUB  0x4b
#define OP_FNMADD  0x4f
#define OP_FP      0x53

/* -----------------------------------------------------------------------
 * merlin_decode — decode one 32-bit instruction
 * ----------------------------------------------------------------------- */
int merlin_decode(u32 insn_word, u32 pc, struct merlin_insn *out)
{
	u32 opcode = insn_word & 0x7f;
	u32 funct3 = bits(insn_word, 14, 12);
	u32 funct7 = bits(insn_word, 31, 25);

	memset(out, 0, sizeof(*out));
	out->raw = insn_word;
	out->pc  = pc;

	/* Compressed (16-bit) instructions have low two bits != 11 */
	if ((insn_word & 3) != 3) {
		out->cls = INSN_UNSUPPORTED;  /* prototype rejects C-ext */
		return 0;
	}

	out->rd  = bits(insn_word, 11, 7);
	out->rs1 = bits(insn_word, 19, 15);
	out->rs2 = bits(insn_word, 24, 20);

	switch (opcode) {

	case OP_LUI:
		out->cls = INSN_LUI;
		out->imm = imm_u(insn_word);
		return 0;

	case OP_AUIPC:
		out->cls = INSN_AUIPC;
		out->imm = imm_u(insn_word);
		return 0;

	case OP_JAL:
		out->cls = INSN_JAL;
		out->imm = imm_j(insn_word);
		return 0;

	case OP_JALR:
		if (funct3 != 0)
			goto invalid;
		out->cls = INSN_JALR;
		out->imm = imm_i(insn_word);
		return 0;

	case OP_BRANCH:
		out->cls = INSN_BRANCH;
		out->imm = imm_b(insn_word);
		switch (funct3) {
		case 0: out->br_kind = BR_BEQ;  break;
		case 1: out->br_kind = BR_BNE;  break;
		case 4: out->br_kind = BR_BLT;  break;
		case 5: out->br_kind = BR_BGE;  break;
		case 6: out->br_kind = BR_BLTU; break;
		case 7: out->br_kind = BR_BGEU; break;
		default: goto invalid;
		}
		return 0;

	case OP_LOAD: {
		out->cls = INSN_LOAD;
		out->imm = imm_i(insn_word);
		switch (funct3) {
		case 0: out->ld_kind = LD_LB;  break;
		case 1: out->ld_kind = LD_LH;  break;
		case 2: out->ld_kind = LD_LW;  break;
		case 3: out->ld_kind = LD_LD;  break;
		case 4: out->ld_kind = LD_LBU; break;
		case 5: out->ld_kind = LD_LHU; break;
		case 6: out->ld_kind = LD_LWU; break;
		default: goto invalid;
		}
		return 0;
	}

	case OP_STORE: {
		out->cls = INSN_STORE;
		out->imm = imm_s(insn_word);
		switch (funct3) {
		case 0: out->st_kind = ST_SB; break;
		case 1: out->st_kind = ST_SH; break;
		case 2: out->st_kind = ST_SW; break;
		case 3: out->st_kind = ST_SD; break;
		default: goto invalid;
		}
		return 0;
	}

	case OP_ALUI:
		switch (funct3) {
		case 0:
			out->cls = INSN_ALU_IMM;
			out->alu_op = ALU_ADD;
			out->imm = imm_i(insn_word);
			return 0;
		case 1:
			out->cls = INSN_SHIFT_IMM;
			out->alu_op = ALU_SLL;
			out->imm = imm_shamt(insn_word, true);
			return 0;
		case 2:
			out->cls = INSN_ALU_IMM;
			out->alu_op = ALU_SLT;
			out->imm = imm_i(insn_word);
			return 0;
		case 3:
			out->cls = INSN_ALU_IMM;
			out->alu_op = ALU_SLTU;
			out->imm = imm_i(insn_word);
			return 0;
		case 4:
			out->cls = INSN_ALU_IMM;
			out->alu_op = ALU_XOR;
			out->imm = imm_i(insn_word);
			return 0;
		case 5:
			out->cls = INSN_SHIFT_IMM;
			out->alu_op = (funct7 & 0x20) ? ALU_SRA : ALU_SRL;
			out->imm = imm_shamt(insn_word, true);
			return 0;
		case 6:
			out->cls = INSN_ALU_IMM;
			out->alu_op = ALU_OR;
			out->imm = imm_i(insn_word);
			return 0;
		case 7:
			out->cls = INSN_ALU_IMM;
			out->alu_op = ALU_AND;
			out->imm = imm_i(insn_word);
			return 0;
		}
		goto invalid;

	case OP_ALUR:
		if (funct7 == 1) {
			/* M extension */
			if (funct3 < 4) {
				out->cls = INSN_MUL;
				static const enum merlin_alu_op mul_ops[] = {
					ALU_MUL, ALU_MULH, ALU_MULHSU, ALU_MULHU,
				};
				out->alu_op = mul_ops[funct3];
			} else {
				out->cls = INSN_DIV;
				static const enum merlin_alu_op div_ops[] = {
					ALU_DIV, ALU_DIVU, ALU_REM, ALU_REMU,
				};
				out->alu_op = div_ops[funct3 - 4];
			}
			return 0;
		}
		{
			out->cls = INSN_ALU_REG;
			static const enum merlin_alu_op alu_ops[8] = {
				ALU_ADD, ALU_SLL, ALU_SLT, ALU_SLTU,
				ALU_XOR, ALU_SRL, ALU_OR,  ALU_AND,
			};
			if (funct7 == 0x20 && (funct3 == 0 || funct3 == 5)) {
				out->alu_op = (funct3 == 0) ? ALU_SUB : ALU_SRA;
			} else if (funct7 == 0) {
				out->alu_op = alu_ops[funct3];
			} else {
				goto invalid;
			}
		}
		return 0;

	case OP_ALUIW:
		out->word_op = true;
		switch (funct3) {
		case 0:
			out->cls = INSN_ALU_IMM_W;
			out->alu_op = ALU_ADD;
			out->imm = imm_i(insn_word);
			return 0;
		case 1:
			out->cls = INSN_ALU_IMM_W;
			out->alu_op = ALU_SLL;
			out->imm = imm_shamt(insn_word, false);
			return 0;
		case 5:
			out->cls = INSN_ALU_IMM_W;
			out->alu_op = (funct7 & 0x20) ? ALU_SRA : ALU_SRL;
			out->imm = imm_shamt(insn_word, false);
			return 0;
		}
		goto invalid;

	case OP_ALUW:
		out->word_op = true;
		if (funct7 == 1) {
			if (funct3 < 4)
				out->cls = INSN_MUL;
			else
				out->cls = INSN_DIV;
			return 0;
		}
		out->cls = INSN_ALU_REG_W;
		if (funct7 == 0x20 && (funct3 == 0 || funct3 == 5)) {
			out->alu_op = (funct3 == 0) ? ALU_SUB : ALU_SRA;
		} else if (funct7 == 0) {
			static const enum merlin_alu_op w_ops[8] = {
				ALU_ADD, ALU_SLL, 0, 0, 0, ALU_SRL, 0, 0,
			};
			out->alu_op = w_ops[funct3];
		} else {
			goto invalid;
		}
		return 0;

	case OP_FENCE:
		if (funct3 == 1) {
			out->cls = INSN_FENCE_I;   /* forbidden */
			return 0;
		}
		out->cls = INSN_FENCE;
		return 0;

	case OP_SYSTEM:
		if (funct3 == 0 && out->rd == 0 && out->rs1 == 0) {
			u32 imm12 = insn_word >> 20;
			if (imm12 == 0) {
				out->cls = INSN_ECALL;
				return 0;
			}
			if (imm12 == 1) {
				out->cls = INSN_EBREAK;
				return 0;
			}
			/* mret/sret/wfi/sfence.vma etc. */
			out->cls = INSN_PRIV;
			return 0;
		}
		/* CSR instructions */
		out->cls = INSN_CSR;
		return 0;

	case OP_AMO:
		/* Atomic (A extension): permitted for default profile but
		 * treated as UNSUPPORTED by the prototype verifier.
		 */
		out->cls = INSN_UNSUPPORTED;
		return 0;

	/* Floating-point opcodes */
	case OP_FLOAT:
	case OP_STORE_FP:
	case OP_FMADD:
	case OP_FMSUB:
	case OP_FNMSUB:
	case OP_FNMADD:
	case OP_FP:
		out->cls = INSN_FLOAT;
		return 0;
	}

invalid:
	out->cls = INSN_INVALID;
	return -1;
}
EXPORT_SYMBOL_GPL(merlin_decode);

bool merlin_insn_permitted_default(const struct merlin_insn *in)
{
	switch (in->cls) {
	case INSN_LUI:
	case INSN_AUIPC:
	case INSN_JAL:
	case INSN_JALR:
	case INSN_BRANCH:
	case INSN_LOAD:
	case INSN_STORE:
	case INSN_ALU_IMM:
	case INSN_SHIFT_IMM:
	case INSN_ALU_REG:
	case INSN_FENCE:
	case INSN_ECALL:
	case INSN_ALU_IMM_W:
	case INSN_ALU_REG_W:
	case INSN_MUL:
	case INSN_DIV:
		return true;
	default:
		return false;
	}
}
EXPORT_SYMBOL_GPL(merlin_insn_permitted_default);

const char *merlin_insn_class_name(enum merlin_insn_class cls)
{
	static const char * const names[] = {
		[INSN_INVALID]      = "INVALID",
		[INSN_UNSUPPORTED]  = "UNSUPPORTED",
		[INSN_LUI]          = "LUI",
		[INSN_AUIPC]        = "AUIPC",
		[INSN_JAL]          = "JAL",
		[INSN_JALR]         = "JALR",
		[INSN_BRANCH]       = "BRANCH",
		[INSN_LOAD]         = "LOAD",
		[INSN_STORE]        = "STORE",
		[INSN_ALU_IMM]      = "ALU_IMM",
		[INSN_SHIFT_IMM]    = "SHIFT_IMM",
		[INSN_ALU_REG]      = "ALU_REG",
		[INSN_FENCE]        = "FENCE",
		[INSN_ECALL]        = "ECALL",
		[INSN_EBREAK]       = "EBREAK",
		[INSN_ALU_IMM_W]    = "ALU_IMM_W",
		[INSN_ALU_REG_W]    = "ALU_REG_W",
		[INSN_MUL]          = "MUL",
		[INSN_DIV]          = "DIV",
		[INSN_CSR]          = "CSR",
		[INSN_PRIV]         = "PRIV",
		[INSN_FENCE_I]      = "FENCE_I",
		[INSN_FLOAT]        = "FLOAT",
	};
	if (cls < ARRAY_SIZE(names) && names[cls])
		return names[cls];
	return "?";
}
EXPORT_SYMBOL_GPL(merlin_insn_class_name);
