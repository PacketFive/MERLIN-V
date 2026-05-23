// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * lab-04/src/verify.c — student-implemented MERLIN-V verifier.
 *
 * SKELETON — students implement the body of merlin_verify_text().
 *
 * The function signature, the abstract domain (in verify.h), and the
 * REJECT() helper are provided.  The TODO blocks below say which
 * properties you must check, but not how to organise the code.
 *
 * Read tools/merlin-verifier/verify.c at the project root for a
 * reference implementation at slightly larger scale.  Do NOT just copy
 * it — you must produce the verifier yourself for this lab.
 *
 * Hint: write the rejection cases first.  See the README.
 */

#include "verify.h"
#include "rv32.h"

#include <stdio.h>
#include <string.h>

/* REJECT helper — students use this for diagnostics.  PROVIDED. */
#define REJECT(fmt, ...) do {                                      \
	fprintf(stderr, "pc=%u: REJECT: " fmt "\n",                \
		in.pc, ##__VA_ARGS__);                             \
	rejected++;                                                \
} while (0)

enum verify_result merlin_verify_text(const uint8_t *text, size_t text_size,
				      const struct verify_cfg *cfg)
{
	struct rval x[32];
	uint32_t rejected = 0;

	if (text_size == 0 || text_size % 4) {
		fprintf(stderr, "REJECT: text size %zu not aligned\n",
			text_size);
		return VERIFY_REJECT;
	}

	/* Initial state: x0 = CONST(0); a0 = PtrCtx(0); sp = PtrStack(0);
	 * others = UNKNOWN.
	 */
	for (int i = 0; i < 32; i++)
		x[i] = (struct rval){ .kind = RVAL_UNKNOWN };
	x[0]      = (struct rval){ .kind = RVAL_CONST,     .val = 0 };
	x[RV_A0]  = (struct rval){ .kind = RVAL_PTR_CTX,   .val = 0 };
	x[RV_SP]  = (struct rval){ .kind = RVAL_PTR_STACK, .val = 0 };

	for (size_t off = 0; off < text_size; off += 4) {
		struct rv_insn in;
		uint32_t w = (uint32_t)text[off]
			   | ((uint32_t)text[off + 1] << 8)
			   | ((uint32_t)text[off + 2] << 16)
			   | ((uint32_t)text[off + 3] << 24);

		if (rv_decode(w, (uint32_t)off, &in) < 0) {
			fprintf(stderr,
				"pc=%zu: REJECT: undecodable %08x\n",
				off, w);
			rejected++;
			continue;
		}

		/* TODO #1 — profile check.
		 *
		 * Reject any instruction class outside the lab subset:
		 *   LUI, AUIPC, JAL, JALR, BRANCH, LOAD, STORE,
		 *   ALU_IMM, SHIFT_IMM, ALU_REG, FENCE, ECALL.
		 *
		 * Use REJECT(...) and `continue` to the next instruction.
		 */

		/* TODO #2 — back-edge check.
		 *
		 * If (in.cls == C_JAL || in.cls == C_BRANCH) && in.imm < 0
		 * and !cfg->allow_back_edges, REJECT.
		 */

		/* TODO #3 — ecall ABI.
		 *
		 * On C_ECALL:
		 *   - require x[RV_A7].kind == RVAL_CONST (else REJECT).
		 *   - require ((1u << x[RV_A7].val) & cfg->helper_allow)
		 *     (else REJECT — helper not in allowlist).
		 *   - on accept: x[RV_A0] = PTR_HELPER_RET(a7.val);
		 *     clobber caller-saved a1..a7, t0..t6 to UNKNOWN.
		 *   - then `continue`.
		 */

		/* TODO #4 — load/store address check.
		 *
		 * On C_LOAD or C_STORE:
		 *   - examine x[in.rs1].kind.
		 *   - REJECT if kind is RVAL_UNKNOWN or RVAL_CONST
		 *     (loads/stores must derive from a typed pointer).
		 *   - For C_LOAD: clobber rd to UNKNOWN.
		 *   - `continue`.
		 */

		/* TODO #5 — ALU effects on abstract state.
		 *
		 * On C_ALU_IMM with alu_op == OP_ADD ("addi"):
		 *   - if rs1 == x0:  rd = CONST(imm)
		 *   - else if x[rs1].kind == CONST:  rd = CONST(x[rs1].val + imm)
		 *   - else if x[rs1].kind in {PTR_CTX, PTR_STACK}:
		 *       rd preserves the root, val += imm.
		 *   - else:  rd = UNKNOWN.
		 *
		 * On C_LUI:  rd = CONST(in.imm).
		 *
		 * On C_AUIPC: rd = UNKNOWN.  (PC-relative; we don't model it.)
		 *
		 * Default for any other in.cls:  if rd != 0, x[rd] = UNKNOWN.
		 *
		 * Always skip writes to x0 (rd == 0).
		 */

		(void)in;  /* remove once TODOs above touch `in` */
	}

	return rejected ? VERIFY_REJECT : VERIFY_OK;
}
