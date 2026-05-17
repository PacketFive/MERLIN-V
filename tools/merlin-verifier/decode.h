/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * decode.h - RV32/RV64 instruction decoder for the MERLIN-V verifier.
 *
 * This is a deliberately MINIMAL decoder.  It covers exactly the
 * instructions a verified MERLIN-V program is permitted to contain
 * (per docs/design/02-isa-and-bytecode.md §§3-4), plus the forbidden
 * ones we need to recognize in order to reject them.  It does NOT
 * cover the C extension yet (Phase 2; we accept rv64imac but the
 * verifier prototype rejects any instruction beginning 0bxxx10 or
 * 0bxxx01 - the C-extension low bits - as "unsupported by prototype").
 *
 * Reference: RISC-V Unprivileged ISA v20191213, "Volume I".
 */
#ifndef MERLIN_VERIFIER_DECODE_H
#define MERLIN_VERIFIER_DECODE_H

#include <stdbool.h>
#include <stdint.h>

enum merlin_insn_class {
	INSN_INVALID = 0,
	INSN_UNSUPPORTED,        /* recognised but not handled by prototype */

	/* RV32I */
	INSN_LUI,
	INSN_AUIPC,
	INSN_JAL,
	INSN_JALR,
	INSN_BRANCH,             /* beq/bne/blt/bge/bltu/bgeu                  */
	INSN_LOAD,               /* lb/lh/lw/lbu/lhu (rv32i+ lwu/ld rv64i)     */
	INSN_STORE,              /* sb/sh/sw (rv64i: sd)                       */
	INSN_ALU_IMM,            /* addi/slti/sltiu/xori/ori/andi              */
	INSN_SHIFT_IMM,          /* slli/srli/srai                             */
	INSN_ALU_REG,            /* add/sub/sll/slt/sltu/xor/srl/sra/or/and    */
	INSN_FENCE,              /* fence (allowed); fence.i (forbidden)       */
	INSN_ECALL,
	INSN_EBREAK,             /* forbidden                                  */

	/* RV64I extras */
	INSN_ALU_IMM_W,          /* addiw / slliw / srliw / sraiw              */
	INSN_ALU_REG_W,          /* addw / subw / sllw / srlw / sraw           */

	/* M extension */
	INSN_MUL,                /* mul/mulh/mulhsu/mulhu / mulw               */
	INSN_DIV,                /* div/divu/rem/remu / divw/...               */

	/* Forbidden classes (we still recognize them so we can reject) */
	INSN_CSR,                /* csrr, csrrw, csrrs, csrrc and immediate forms */
	INSN_PRIV,               /* mret/sret/wfi/sfence.vma/hfence.*          */
	INSN_FENCE_I,
	INSN_FLOAT,              /* any F/D opcode                             */
};

enum merlin_branch_kind {
	BR_BEQ, BR_BNE, BR_BLT, BR_BGE, BR_BLTU, BR_BGEU,
};

enum merlin_load_kind {
	LD_LB = 0, LD_LH = 1, LD_LW = 2, LD_LD = 3,
	LD_LBU = 4, LD_LHU = 5, LD_LWU = 6,
};

enum merlin_store_kind {
	ST_SB = 0, ST_SH = 1, ST_SW = 2, ST_SD = 3,
};

enum merlin_alu_op {
	ALU_ADD, ALU_SUB, ALU_SLL, ALU_SLT, ALU_SLTU,
	ALU_XOR, ALU_SRL, ALU_SRA, ALU_OR,  ALU_AND,
	ALU_MUL, ALU_DIV, ALU_DIVU, ALU_REM, ALU_REMU,
	ALU_MULH, ALU_MULHU, ALU_MULHSU,
};

/*
 * Decoded instruction.  Fields are sparsely populated by class; readers
 * MUST check .cls before consulting fields.
 */
struct merlin_insn {
	uint32_t  raw;
	uint32_t  pc;          /* byte offset within the program text         */
	enum merlin_insn_class cls;

	uint8_t   rd, rs1, rs2;
	int64_t   imm;         /* sign-extended; meaning per cls              */

	/* per-class extras */
	enum merlin_branch_kind br_kind;
	enum merlin_load_kind   ld_kind;
	enum merlin_store_kind  st_kind;
	enum merlin_alu_op      alu_op;
	bool                    word_op;  /* true for *W variants            */
};

/*
 * Decode one instruction at *(insn_word).  Returns:
 *   0 on success (out is populated, including INSN_UNSUPPORTED, _PRIV,
 *     _CSR, _FENCE_I, _FLOAT, _EBREAK which the verifier rejects but
 *     decode considers "successfully recognised");
 *  -1 on hopeless decode error (bits make no sense at all).
 *
 * insn_word is little-endian (verified at higher level via ELF EI_DATA).
 */
int merlin_decode(uint32_t insn_word, uint32_t pc, struct merlin_insn *out);

/* Pretty name for diagnostics. */
const char *merlin_insn_class_name(enum merlin_insn_class cls);

/* True if the instruction class is permitted in the default verifier
 * profile (linux-rv64/default).  Programs containing any not-permitted
 * class are rejected.
 */
bool merlin_insn_permitted_default(const struct merlin_insn *in);

#endif /* MERLIN_VERIFIER_DECODE_H */
