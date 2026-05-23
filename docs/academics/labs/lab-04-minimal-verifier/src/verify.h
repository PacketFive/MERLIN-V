/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * lab-04/src/verify.h — abstract-interpretation verifier interface.
 *
 * PROVIDED — do not modify.
 *
 * The decoder is reused from lab-03 (struct rv_insn, rv_decode).
 */
#ifndef LAB_VERIFY_H
#define LAB_VERIFY_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "interp.h"   /* from lab-03: struct rv_insn, rv_decode */

/* Abstract register domain (see lab-04 README). */
enum rval_kind {
	RVAL_UNKNOWN = 0,
	RVAL_CONST,
	RVAL_PTR_CTX,
	RVAL_PTR_STACK,
	RVAL_PTR_HELPER_RET,
};

struct rval {
	enum rval_kind kind;
	uint32_t       val;    /* CONST: value | PTR: offset | HRET: helper id */
};

#define MAX_HELPER_ID 31

struct verify_cfg {
	/* Bitmask of permitted helpers (bit i = helper id i allowed). */
	uint32_t helper_allow;
	uint16_t max_stack_bytes;
	bool     allow_back_edges;
};

enum verify_result {
	VERIFY_OK     = 0,
	VERIFY_REJECT = 1,
};

/* Verify a .text section.
 *
 * text, text_size: raw RV32 LE instruction bytes.
 * cfg: configuration.
 *
 * Prints diagnostics to stderr on rejection.
 * Returns VERIFY_OK or VERIFY_REJECT.
 */
enum verify_result merlin_verify_text(const uint8_t *text, size_t text_size,
				      const struct verify_cfg *cfg);

#endif /* LAB_VERIFY_H */
