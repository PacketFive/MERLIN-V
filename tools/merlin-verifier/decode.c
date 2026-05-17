// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * decode.c - RV32I/RV64I/M instruction decoder for the verifier.
 *
 * Field-extraction helpers and a single dispatch table keyed on the
 * 7-bit major opcode.  We do not aim to be a complete RISC-V
 * disassembler; we aim to recognize every instruction a valid
 * MERLIN-V program may contain, plus every instruction we must
 * reject by class.
 */
#include "decode.h"

#include <stdint.h>

/* --- bit-field extractors --- */
static inline uint32_t bits(uint32_t w, int hi, int lo)
{
	return (w >> lo) & ((1u << (hi - lo + 1)) - 1);
}
static inline int32_t sext(uint32_t v, int width)
{
	uint32_t sign = 1u << (width - 1);
	return (int32_t)((v ^ sign) - sign);
}

/* --- immediate forms (RISC-V Vol I, §2.2) --- */
static int32_t imm_I(uint32_t w)  /* I-type, 12-bit sign-extended */
{
	return sext(bits(w, 31, 20), 12);
}
static int32_t imm_S(uint32_t w)  /* S-type */
{
	uint32_t v = (bits(w, 31, 25) << 5) | bits(w, 11, 7);
	return sext(v, 12);
}
static int32_t imm_B(uint32_t w)  /* B-type, multiple of 2 */
{
	uint32_t v = (bits(w, 31, 31) << 12)
		   | (bits(w,  7,  7) << 11)
		   | (bits(w, 30, 25) << 5)
		   | (bits(w, 11,  8) << 1);
	return sext(v, 13);
}
static int32_t imm_U(uint32_t w)  /* U-type, top 20 bits */
{
	return (int32_t)(w & 0xfffff000u);
}
static int32_t imm_J(uint32_t w)  /* J-type, multiple of 2 */
{
	uint32_t v = (bits(w, 31, 31) << 20)
		   | (bits(w, 19, 12) << 12)
		   | (bits(w, 20, 20) << 11)
		   | (bits(w, 30, 21) << 1);
	return sext(v, 21);
}

/* --- main decode --- */
int merlin_decode(uint32_t w, uint32_t pc, struct merlin_insn *out)
{
	uint32_t op     = bits(w, 6, 0);
	uint32_t funct3 = bits(w, 14, 12);
	uint32_t funct7 = bits(w, 31, 25);

	out->raw     = w;
	out->pc      = pc;
	out->cls     = INSN_INVALID;
	out->rd      = bits(w, 11, 7);
	out->rs1     = bits(w, 19, 15);
	out->rs2     = bits(w, 24, 20);
	out->imm     = 0;
	out->word_op = false;

	/* RV32C / RVC bits 0..1 indicate 16-bit compressed instructions.
	 * The prototype rejects them as INSN_UNSUPPORTED (Phase 2).
	 */
	if ((w & 0x3) != 0x3) {
		out->cls = INSN_UNSUPPORTED;
		return 0;
	}

	switch (op) {
	case 0x37:  /* LUI    rd, imm20 */
		out->cls = INSN_LUI;
		out->imm = imm_U(w);
		return 0;
	case 0x17:  /* AUIPC  rd, imm20 */
		out->cls = INSN_AUIPC;
		out->imm = imm_U(w);
		return 0;
	case 0x6f:  /* JAL    rd, imm */
		out->cls = INSN_JAL;
		out->imm = imm_J(w);
		return 0;
	case 0x67:  /* JALR   rd, rs1, imm */
		if (funct3 != 0) { out->cls = INSN_INVALID; return 0; }
		out->cls = INSN_JALR;
		out->imm = imm_I(w);
		return 0;
	case 0x63:  /* B-type */
		switch (funct3) {
		case 0: out->br_kind = BR_BEQ;  break;
		case 1: out->br_kind = BR_BNE;  break;
		case 4: out->br_kind = BR_BLT;  break;
		case 5: out->br_kind = BR_BGE;  break;
		case 6: out->br_kind = BR_BLTU; break;
		case 7: out->br_kind = BR_BGEU; break;
		default: out->cls = INSN_INVALID; return 0;
		}
		out->cls = INSN_BRANCH;
		out->imm = imm_B(w);
		return 0;
	case 0x03:  /* loads */
		if (funct3 > 6) { out->cls = INSN_INVALID; return 0; }
		out->cls     = INSN_LOAD;
		out->ld_kind = (enum merlin_load_kind)funct3;
		out->imm     = imm_I(w);
		return 0;
	case 0x23:  /* stores */
		if (funct3 > 3) { out->cls = INSN_INVALID; return 0; }
		out->cls     = INSN_STORE;
		out->st_kind = (enum merlin_store_kind)funct3;
		out->imm     = imm_S(w);
		return 0;
	case 0x13:  /* I-type ALU and shifts */
		switch (funct3) {
		case 0: out->cls = INSN_ALU_IMM; out->alu_op = ALU_ADD;  out->imm = imm_I(w); return 0;
		case 2: out->cls = INSN_ALU_IMM; out->alu_op = ALU_SLT;  out->imm = imm_I(w); return 0;
		case 3: out->cls = INSN_ALU_IMM; out->alu_op = ALU_SLTU; out->imm = imm_I(w); return 0;
		case 4: out->cls = INSN_ALU_IMM; out->alu_op = ALU_XOR;  out->imm = imm_I(w); return 0;
		case 6: out->cls = INSN_ALU_IMM; out->alu_op = ALU_OR;   out->imm = imm_I(w); return 0;
		case 7: out->cls = INSN_ALU_IMM; out->alu_op = ALU_AND;  out->imm = imm_I(w); return 0;
		case 1: /* SLLI */
			out->cls = INSN_SHIFT_IMM;
			out->alu_op = ALU_SLL;
			out->imm = bits(w, 25, 20);  /* shamt[6:0] = bits 25..20 RV64 */
			return 0;
		case 5: /* SRLI / SRAI */
			out->cls = INSN_SHIFT_IMM;
			out->alu_op = (bits(w, 30, 30)) ? ALU_SRA : ALU_SRL;
			out->imm = bits(w, 25, 20);
			return 0;
		}
		break;
	case 0x33:  /* R-type ALU */
		if (funct7 == 0x01) {  /* M extension */
			switch (funct3) {
			case 0: out->cls = INSN_MUL;  out->alu_op = ALU_MUL;     return 0;
			case 1: out->cls = INSN_MUL;  out->alu_op = ALU_MULH;    return 0;
			case 2: out->cls = INSN_MUL;  out->alu_op = ALU_MULHSU;  return 0;
			case 3: out->cls = INSN_MUL;  out->alu_op = ALU_MULHU;   return 0;
			case 4: out->cls = INSN_DIV;  out->alu_op = ALU_DIV;     return 0;
			case 5: out->cls = INSN_DIV;  out->alu_op = ALU_DIVU;    return 0;
			case 6: out->cls = INSN_DIV;  out->alu_op = ALU_REM;     return 0;
			case 7: out->cls = INSN_DIV;  out->alu_op = ALU_REMU;    return 0;
			}
			out->cls = INSN_INVALID;
			return 0;
		}
		out->cls = INSN_ALU_REG;
		switch (funct3) {
		case 0: out->alu_op = (funct7 == 0x20) ? ALU_SUB : ALU_ADD; break;
		case 1: out->alu_op = ALU_SLL;  break;
		case 2: out->alu_op = ALU_SLT;  break;
		case 3: out->alu_op = ALU_SLTU; break;
		case 4: out->alu_op = ALU_XOR;  break;
		case 5: out->alu_op = (funct7 == 0x20) ? ALU_SRA : ALU_SRL; break;
		case 6: out->alu_op = ALU_OR;   break;
		case 7: out->alu_op = ALU_AND;  break;
		}
		return 0;
	case 0x1b:  /* RV64I W-form I-type */
		out->cls     = INSN_ALU_IMM_W;
		out->word_op = true;
		switch (funct3) {
		case 0: out->alu_op = ALU_ADD; out->imm = imm_I(w); return 0;
		case 1: out->alu_op = ALU_SLL; out->imm = bits(w, 24, 20); return 0;
		case 5:
			out->alu_op = bits(w, 30, 30) ? ALU_SRA : ALU_SRL;
			out->imm = bits(w, 24, 20);
			return 0;
		}
		out->cls = INSN_INVALID;
		return 0;
	case 0x3b:  /* RV64I W-form R-type */
		out->cls     = INSN_ALU_REG_W;
		out->word_op = true;
		if (funct7 == 0x01) {
			out->cls = INSN_MUL;
			switch (funct3) {
			case 0: out->alu_op = ALU_MUL;  return 0;
			case 4: out->cls = INSN_DIV; out->alu_op = ALU_DIV;  return 0;
			case 5: out->cls = INSN_DIV; out->alu_op = ALU_DIVU; return 0;
			case 6: out->cls = INSN_DIV; out->alu_op = ALU_REM;  return 0;
			case 7: out->cls = INSN_DIV; out->alu_op = ALU_REMU; return 0;
			}
			out->cls = INSN_INVALID;
			return 0;
		}
		switch (funct3) {
		case 0: out->alu_op = (funct7 == 0x20) ? ALU_SUB : ALU_ADD; return 0;
		case 1: out->alu_op = ALU_SLL; return 0;
		case 5: out->alu_op = (funct7 == 0x20) ? ALU_SRA : ALU_SRL; return 0;
		}
		out->cls = INSN_INVALID;
		return 0;
	case 0x0f:  /* fence / fence.i */
		if (funct3 == 0) {
			out->cls = INSN_FENCE;
			return 0;
		}
		if (funct3 == 1) {
			out->cls = INSN_FENCE_I;
			return 0;
		}
		out->cls = INSN_INVALID;
		return 0;
	case 0x73:  /* system: ecall/ebreak/csr/priv */
		if (funct3 == 0) {
			/* ECALL/EBREAK/MRET/SRET/WFI/SFENCE.VMA */
			if (w == 0x00000073) { out->cls = INSN_ECALL;  return 0; }
			if (w == 0x00100073) { out->cls = INSN_EBREAK; return 0; }
			if (w == 0x30200073) { out->cls = INSN_PRIV; return 0; }  /* mret */
			if (w == 0x10200073) { out->cls = INSN_PRIV; return 0; }  /* sret */
			if (w == 0x10500073) { out->cls = INSN_PRIV; return 0; }  /* wfi  */
			if ((w & 0xfe007fff) == 0x12000073)
			                     { out->cls = INSN_PRIV; return 0; }  /* sfence.vma */
			out->cls = INSN_PRIV;
			return 0;
		}
		/* CSR* */
		out->cls = INSN_CSR;
		return 0;
	case 0x07: case 0x27:   /* FP loads/stores */
	case 0x43: case 0x47: case 0x4b: case 0x4f:  /* FMADD/FMSUB/FNMSUB/FNMADD */
	case 0x53:              /* OP-FP */
		out->cls = INSN_FLOAT;
		return 0;
	}

	out->cls = INSN_INVALID;
	return 0;
}

const char *merlin_insn_class_name(enum merlin_insn_class cls)
{
	switch (cls) {
	case INSN_INVALID:     return "invalid";
	case INSN_UNSUPPORTED: return "unsupported (RVC?)";
	case INSN_LUI:         return "lui";
	case INSN_AUIPC:       return "auipc";
	case INSN_JAL:         return "jal";
	case INSN_JALR:        return "jalr";
	case INSN_BRANCH:      return "branch";
	case INSN_LOAD:        return "load";
	case INSN_STORE:       return "store";
	case INSN_ALU_IMM:     return "alu-imm";
	case INSN_SHIFT_IMM:   return "shift-imm";
	case INSN_ALU_REG:     return "alu-reg";
	case INSN_ALU_IMM_W:   return "alu-imm-w";
	case INSN_ALU_REG_W:   return "alu-reg-w";
	case INSN_MUL:         return "mul";
	case INSN_DIV:         return "div";
	case INSN_FENCE:       return "fence";
	case INSN_ECALL:       return "ecall";
	case INSN_EBREAK:      return "ebreak (FORBIDDEN)";
	case INSN_CSR:         return "csr* (FORBIDDEN)";
	case INSN_PRIV:        return "privileged (FORBIDDEN)";
	case INSN_FENCE_I:     return "fence.i (FORBIDDEN)";
	case INSN_FLOAT:       return "FP (FORBIDDEN in default profile)";
	}
	return "?";
}

bool merlin_insn_permitted_default(const struct merlin_insn *in)
{
	switch (in->cls) {
	case INSN_EBREAK:
	case INSN_CSR:
	case INSN_PRIV:
	case INSN_FENCE_I:
	case INSN_FLOAT:
	case INSN_INVALID:
		return false;
	case INSN_UNSUPPORTED:
		/* RVC instructions: not yet supported by this prototype,
		 * but they ARE in the linux-rv64 profile.  Reject here
		 * with a clear message; full support is a Phase 2 task.
		 */
		return false;
	default:
		return true;
	}
}
