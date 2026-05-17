/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * verify.h - prototype abstract-interpretation verifier for MERLIN-V.
 *
 * Per docs/design/06-verifier.md §3, the verifier's domain is:
 *
 *   register -> { Unknown, Scalar{range,align}, Ptr{root,off-range} }
 *
 * For the prototype, we implement a useful subset:
 *
 *   - Unknown (top)
 *   - ConstU64(value)            full constant value known
 *   - ScalarUnknown              integer with unknown value
 *   - PtrCtx{off}                pointer to ctx, fixed offset
 *   - PtrStack{off}              pointer to current frame, fixed offset
 *   - PtrHelperRet{helper_id}    just-returned-from-helper pointer
 *
 * This is enough to:
 *   - prove the helper-call pattern: `li a7, K; ecall` with K constant
 *     in the allowlist, and reject any ecall without it;
 *   - prove every load/store address derives from a typed root;
 *   - reject any forbidden instruction (decode.c does the classifying);
 *   - reject any back-edge (we do not yet support bounded loops).
 *
 * The output is one of:
 *   MERLIN_VERIFY_OK / MERLIN_VERIFY_REJECT, plus a human-readable
 *   diagnostic stream into stderr.
 */
#ifndef MERLIN_VERIFIER_VERIFY_H
#define MERLIN_VERIFIER_VERIFY_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "decode.h"

#define MERLIN_NR_HELPERS  4096    /* indexable allowlist                  */

enum merlin_verify_result {
	MERLIN_VERIFY_OK     = 0,
	MERLIN_VERIFY_REJECT = 1,
};

struct merlin_verifier_cfg {
	/* Bitset of permitted helper IDs.  Bit n set => helper n is in
	 * the program-type allowlist for this verification run.
	 */
	uint8_t helper_allow[MERLIN_NR_HELPERS / 8];

	/* Maximum stack frame the program may declare.  Profile defaults:
	 *   linux-rv64/default      512
	 *   linux-rv64/sleepable    8192
	 *   rtos-rv32/zephyr        256
	 */
	uint32_t max_stack_bytes;

	/* Allow back-edges? RFC v1 default is false (no loops yet);
	 * Phase 2 will gate on merlin_loop() helper.
	 */
	bool allow_back_edges;

	bool verbose;
};

/*
 * Verify one .text.merlin.* section.  text/text_size is the decoded
 * instruction stream as a contiguous LE byte array; text_offset is
 * the byte offset within the section (for diagnostic addresses).
 *
 * Returns MERLIN_VERIFY_OK iff all checks pass.  Emits diagnostics
 * to stderr (when cfg->verbose) and to *summary (a short final line).
 */
enum merlin_verify_result merlin_verify_text(
	const uint8_t *text,
	size_t text_size,
	uint32_t text_offset,
	const struct merlin_verifier_cfg *cfg,
	char *summary, size_t summary_len);

/* Convenience: set up a verifier_cfg with a default helper allowlist
 * derived from the program type (best-effort; MVDP today).
 */
void merlin_verifier_cfg_init_for_mvdp(struct merlin_verifier_cfg *cfg);

#endif /* MERLIN_VERIFIER_VERIFY_H */
