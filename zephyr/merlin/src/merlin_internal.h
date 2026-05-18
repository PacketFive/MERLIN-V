/* SPDX-License-Identifier: Apache-2.0 */
/*
 * src/merlin_internal.h — Zephyr-internal types for the MERLIN-V runtime.
 *
 * Mirrors the structure of kernel/merlin/include/merlin_internal.h but
 * adapted to Zephyr APIs and to the rtos-rv32 profile (single profile
 * supported; no multi-host JIT; no IDR — fixed-size slot table).
 */
#ifndef MERLIN_RUNTIME_INTERNAL_H_
#define MERLIN_RUNTIME_INTERNAL_H_

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifndef MERLIN_HOST_SMOKE
#include <zephyr/kernel.h>
#else
/* Host smoke compile: fake the Zephyr Kconfig values we use. */
#ifndef CONFIG_MERLIN_MAX_STACK_BYTES
#define CONFIG_MERLIN_MAX_STACK_BYTES 256
#endif
#ifndef CONFIG_MERLIN_MAX_PROG_BYTES
#define CONFIG_MERLIN_MAX_PROG_BYTES  16384
#endif
#ifndef CONFIG_MERLIN_LOG_LEVEL
#define CONFIG_MERLIN_LOG_LEVEL       1
#endif
#endif

#include "merlin/merlin.h"

/* -----------------------------------------------------------------------
 * Decoder API
 * ----------------------------------------------------------------------- */
enum merlin_insn_class {
	INSN_INVALID = 0,
	INSN_UNSUPPORTED,
	INSN_LUI, INSN_AUIPC,
	INSN_JAL, INSN_JALR,
	INSN_BRANCH,
	INSN_LOAD, INSN_STORE,
	INSN_ALU_IMM, INSN_SHIFT_IMM,
	INSN_ALU_REG,
	INSN_FENCE,
	INSN_ECALL, INSN_EBREAK,
	INSN_ALU_IMM_W, INSN_ALU_REG_W,
	INSN_MUL, INSN_DIV,
	INSN_CSR, INSN_PRIV, INSN_FENCE_I, INSN_FLOAT,
};

enum merlin_alu_op {
	ALU_ADD = 0, ALU_SUB, ALU_SLL, ALU_SLT, ALU_SLTU,
	ALU_XOR, ALU_SRL, ALU_SRA, ALU_OR,  ALU_AND,
	ALU_MUL, ALU_DIV, ALU_DIVU, ALU_REM, ALU_REMU,
	ALU_MULH, ALU_MULHU, ALU_MULHSU,
};

struct merlin_insn {
	uint32_t raw;
	uint32_t pc;
	enum merlin_insn_class cls;
	uint8_t  rd, rs1, rs2;
	int32_t  imm;
	enum merlin_alu_op alu_op;
	bool     word_op;
};

int  merlin_decode(uint32_t insn_word, uint32_t pc, struct merlin_insn *out);
bool merlin_insn_permitted_rtos(const struct merlin_insn *in);
const char *merlin_insn_class_name(enum merlin_insn_class cls);

/* -----------------------------------------------------------------------
 * Verifier API
 * ----------------------------------------------------------------------- */
enum merlin_rval_kind {
	RVAL_UNKNOWN = 0,
	RVAL_CONST,
	RVAL_PTR_CTX,
	RVAL_PTR_STACK,
	RVAL_PTR_HELPER_RET,
};

struct merlin_rval {
	enum merlin_rval_kind kind;
	uint32_t val;     /* CONST or PTR offset */
};

struct merlin_vstate {
	struct merlin_rval x[32];
};

struct merlin_verifier_cfg {
	/* 4096 helper ids / 8 = 512 bytes — too big for RTOS; pack into 64 bytes (id 0..511). */
	uint8_t  helper_allow[64];
	uint16_t max_helper_id;     /* 511 for the RTOS profile             */
	uint16_t max_stack_bytes;
	bool     allow_back_edges;
	bool     verbose;
	uint32_t insns_seen;        /* OUT: insn count                       */
	uint32_t rejected;          /* OUT: reject count                     */
	char    *log_buf;
	uint32_t log_buf_sz;
	uint32_t log_pos;
};

int merlin_verify_text(const uint8_t *text, size_t text_size,
		       struct merlin_verifier_cfg *cfg);

void merlin_verifier_cfg_for_prog(struct merlin_verifier_cfg *cfg,
				  enum merlin_rtos_prog_type pt);

/* -----------------------------------------------------------------------
 * Program slot
 * ----------------------------------------------------------------------- */
struct merlin_prog {
	bool      in_use;
	uint32_t  id;
	uint32_t  text_bytes;
	enum merlin_rtos_prog_type prog_type;
	char      name[16];

	/* Executable text — for Zephyr/RISC-V we point directly at a buffer
	 * we copied into a region that is mapped executable (rodata on most
	 * Zephyr boards; .text on boards that pin the heap RWX).
	 */
	uint8_t  *text;
	uint32_t  text_alloc;

	/* Cached function pointer for fast invocation */
	uint64_t (*fn)(void *ctx);

	/* Runtime stats */
	uint64_t  run_cnt;
	uint64_t  run_time_ns;
};

/* -----------------------------------------------------------------------
 * Runtime (loader.c / runtime.c)
 * ----------------------------------------------------------------------- */
int merlin_runtime_install(struct merlin_prog *prog,
			   const uint8_t *text, size_t text_len);
void merlin_runtime_uninstall(struct merlin_prog *prog);

/* -----------------------------------------------------------------------
 * Dispatch (dispatch.c)
 * ----------------------------------------------------------------------- */
uint64_t merlin_dispatch_helper(uint32_t id,
				uint64_t a0, uint64_t a1, uint64_t a2,
				uint64_t a3, uint64_t a4, uint64_t a5);

/* -----------------------------------------------------------------------
 * Logging
 * ----------------------------------------------------------------------- */
void merlin_log(struct merlin_verifier_cfg *cfg, const char *fmt, ...);

#endif /* MERLIN_RUNTIME_INTERNAL_H_ */
