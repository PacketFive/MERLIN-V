/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * common/rv32.h — RV32 register names and opcode constants.
 *
 * PROVIDED — do not modify.
 */
#ifndef MERLIN_LABS_RV32_H
#define MERLIN_LABS_RV32_H

#include <stdint.h>

/* RISC-V architectural registers (RV32I) */
enum rv_reg {
	RV_ZERO = 0,  RV_RA,    RV_SP,    RV_GP,
	RV_TP,        RV_T0,    RV_T1,    RV_T2,
	RV_S0,        RV_S1,
	RV_A0,        RV_A1,    RV_A2,    RV_A3,
	RV_A4,        RV_A5,    RV_A6,    RV_A7,
	RV_S2,        RV_S3,    RV_S4,    RV_S5,
	RV_S6,        RV_S7,    RV_S8,    RV_S9,
	RV_S10,       RV_S11,   RV_T3,    RV_T4,
	RV_T5,        RV_T6,
};

static inline const char *rv_reg_name(unsigned r)
{
	static const char *names[32] = {
		"zero","ra","sp","gp","tp","t0","t1","t2",
		"s0","s1","a0","a1","a2","a3","a4","a5",
		"a6","a7","s2","s3","s4","s5","s6","s7",
		"s8","s9","s10","s11","t3","t4","t5","t6",
	};
	return r < 32 ? names[r] : "?";
}

/* Opcodes (low 7 bits) used by RV32I/M */
#define RV_OP_LOAD     0x03
#define RV_OP_FENCE    0x0f
#define RV_OP_ALUI     0x13
#define RV_OP_AUIPC    0x17
#define RV_OP_STORE    0x23
#define RV_OP_ALUR     0x33
#define RV_OP_LUI      0x37
#define RV_OP_BRANCH   0x63
#define RV_OP_JALR     0x67
#define RV_OP_JAL      0x6f
#define RV_OP_SYSTEM   0x73

/* Helper IDs the MERLIN-V courseware uses */
#define MERLIN_HELPER_TRACE_PRINT  1
#define MERLIN_HELPER_GET_TIME_NS  2
#define MERLIN_HELPER_RAND_U32     3

/* Bitfield extraction (used by decoders) */
static inline uint32_t rv_bits(uint32_t v, int hi, int lo)
{
	return (v >> lo) & ((1u << (hi - lo + 1)) - 1);
}

static inline int32_t rv_sext(uint32_t v, int width)
{
	int shift = 32 - width;
	return (int32_t)(v << shift) >> shift;
}

#endif /* MERLIN_LABS_RV32_H */
