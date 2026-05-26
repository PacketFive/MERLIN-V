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

#define MERLIN_NR_HELPERS    4096
#define MERLIN_NR_KFUNCS     4096
#define MERLIN_NR_CALLBACKS  4096

/* The well-known helper id for "this is a bounded loop". */
#define MERLIN_HELPER_LOOP_BOUND 0x0142

/* The well-known helper id for "resolve kfunc id (in a0) into a
 * PTR_KFUNC_SLOT in a0".  See docs/design/15-verifier-phase2.md §A2.
 */
#define MERLIN_HELPER_KFUNC_RESOLVE 0x0143

/* The well-known helper id for "run callback cb_id (in a1) up to N
 * (a0) times with (a2) as ctx".  See docs/design/15-verifier-phase2.md §A3.
 *
 * Canonical sequence:
 *   addi a0, x0, N         ; max iterations (must be const ≥ 1)
 *   addi a1, x0, CB_ID     ; callback id (must be const, in callback_allow)
 *   addi a7, x0, 0x0144    ; MERLIN_HELPER_LOOP_CB
 *   ecall                  ; loop; a0 → aggregate return (SCALAR_TOP)
 *
 * The immediate fall-through block is also marked as a permitted loop
 * header (same mechanism as MERLIN_HELPER_LOOP_BOUND).
 *
 * The callback body is a separate MERLIN program section identified
 * by the .text.merlin.cb.* naming convention.  The verifier runs it
 * with a callback entry state: a0 = scalar [0, INT64_MAX] (loop
 * index), a1 = PTR_CTX, all other argument registers UNINIT.
 */
#define MERLIN_HELPER_LOOP_CB 0x0144

/* ----------------------------------------------------------------------
 * Abstract value domain (per-register).
 * ---------------------------------------------------------------------- */
enum merlin_rval_kind {
	RVAL_UNINIT = 0,
	RVAL_SCALAR,
	RVAL_PTR_CTX,
	RVAL_PTR_STACK,
	RVAL_PTR_HELPER_RET,
	RVAL_PTR_KFUNC_SLOT,
};

struct merlin_rval {
	enum merlin_rval_kind kind;
	struct merlin_scalar s; /* SCALAR: value range + tnum   */
	int64_t off_min; /* PTR_*:  signed offset range  */
	int64_t off_max;
	uint32_t helper_id; /* PTR_HELPER_RET, PTR_KFUNC_SLOT id */
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
	uint8_t kfunc_allow[MERLIN_NR_KFUNCS / 8];
	uint8_t callback_allow[MERLIN_NR_CALLBACKS / 8]; /* Phase-3.A3 */
	uint32_t max_stack_bytes;
	bool allow_back_edges;
	bool is_callback_body; /* Phase-3.A3: use callback entry state */
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
