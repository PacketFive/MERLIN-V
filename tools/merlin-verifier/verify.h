/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * verify.h - abstract-interpretation verifier for MERLIN-V (Phase 2).
 *
 * Phase 2 (this file) adds, over the linear-pass Phase 1:
 *
 *   - CFG construction with reducibility check (cfg.h/c).
 *   - Worklist abstract interpretation over basic blocks.
 *   - State joins at block entry (per-register tnum + range merge).
 *   - Scalar range tracking (signed + unsigned + tnum mask).
 *   - Pointer offset bounds (off_min..off_max) per typed root.
 *   - merlin_loop_bound() helper recognition for bounded loops:
 *
 *         li a7, MERLIN_HELPER_LOOP_BOUND   (id 0x0142)
 *         li a0, <const>                    (max iterations)
 *         ecall
 *
 *     A subsequent back edge whose loop header is the basic block
 *     immediately following the ecall is accepted; the runtime step
 *     cap enforces the bound dynamically.
 *
 * Per docs/design/06-verifier.md the verifier domain mirrors the
 * eBPF verifier's abstract domain (tnum + signed/unsigned range +
 * pointer provenance + offset bounds) so that the eventual
 * factoring into a shared kernel module (Option A) is a syntactic
 * change rather than a semantic one.
 */
#ifndef MERLIN_VERIFIER_VERIFY_H
#define MERLIN_VERIFIER_VERIFY_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "decode.h"
#include "tnum.h"

#define MERLIN_NR_HELPERS 4096

/* The well-known helper id for "this is a bounded loop". */
#define MERLIN_HELPER_LOOP_BOUND 0x0142

/* ----------------------------------------------------------------------
 * Abstract value domain (per-register).
 * ---------------------------------------------------------------------- */
enum merlin_rval_kind {
	RVAL_UNINIT = 0,
	RVAL_SCALAR,
	RVAL_PTR_CTX,
	RVAL_PTR_STACK,
	RVAL_PTR_HELPER_RET,
};

struct merlin_rval {
	enum merlin_rval_kind kind;
	struct merlin_scalar s; /* SCALAR: value range + tnum   */
	int64_t off_min; /* PTR_*:  signed offset range  */
	int64_t off_max;
	uint32_t helper_id; /* PTR_HELPER_RET: helper id    */
};

struct merlin_vstate {
	struct merlin_rval x[32];
	bool valid; /* false == bottom (no path)    */
};

/* ----------------------------------------------------------------------
 * Configuration.
 * ---------------------------------------------------------------------- */
struct merlin_verifier_cfg {
	uint8_t helper_allow[MERLIN_NR_HELPERS / 8];
	uint32_t max_stack_bytes;
	bool allow_back_edges;
	bool verbose;

	uint32_t step_cap; /* total transfer-function invocations */
};

enum merlin_verify_result {
	MERLIN_VERIFY_OK = 0,
	MERLIN_VERIFY_REJECT = 1,
};

enum merlin_verify_result
merlin_verify_text(const uint8_t *text, size_t text_size, uint32_t text_offset,
		   const struct merlin_verifier_cfg *cfg, char *summary,
		   size_t summary_len);

void merlin_verifier_cfg_init_for_mvdp(struct merlin_verifier_cfg *cfg);

#endif /* MERLIN_VERIFIER_VERIFY_H */
