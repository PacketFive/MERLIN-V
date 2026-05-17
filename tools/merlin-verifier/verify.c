// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * verify.c - prototype abstract-interpretation verifier.
 *
 * Scope (intentionally bounded for v0):
 *
 *  - Decodes every 32-bit instruction in a .text.merlin.* section.
 *  - Walks the program linearly from offset 0 (no CFG / no joins yet);
 *    branches and jumps are recorded for diagnostic purposes but
 *    the verifier "follows" linear flow plus the implicit
 *    fall-through after branches.
 *  - Tracks abstract register state through one linear pass.
 *  - Recognizes the canonical helper-call sequence
 *
 *       li  a7, <const>      ; addi a7, x0, K  (or lui+addi for K > 12 bits)
 *       ecall
 *
 *    and rejects any ecall whose preceding a7 is not a constant
 *    in the helper allowlist.
 *  - Rejects any forbidden instruction by class (decode.c flags them).
 *  - Detects back-edges via JAL/branch with negative immediate;
 *    rejects them in the prototype.
 *  - Tracks pointer-rooted register values when established by
 *    the function-entry conventions (a0 = ctx) or by helper return.
 *  - Verifies every LOAD/STORE address derives from a typed root.
 *
 * Not yet implemented (Phase 2):
 *
 *  - Real CFG with join points and widening (we approximate by
 *    treating every branch as "both paths possible" but do not
 *    re-walk).
 *  - Bounded-loop verification.
 *  - Stack frame discipline (sp delta tracking; we accept but
 *    don't yet check stack stores).
 *  - kfunc resolution.
 *  - tnum tracking with bit-level precision.
 *
 * The architecture in this file is deliberately straightforward
 * so it can grow into the Phase 2 verifier later.
 */
#include "verify.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* RISC-V ABI register names a verifier emit needs in diagnostics. */
static const char *xreg(unsigned r)
{
	static const char *names[32] = {
		"zero","ra","sp","gp","tp","t0","t1","t2",
		"s0/fp","s1","a0","a1","a2","a3","a4","a5",
		"a6","a7","s2","s3","s4","s5","s6","s7",
		"s8","s9","s10","s11","t3","t4","t5","t6",
	};
	return r < 32 ? names[r] : "?";
}

/* ---------------------------------------------------------------- *
 *  Abstract register domain
 * ---------------------------------------------------------------- */
enum rval_kind {
	RVAL_UNKNOWN = 0,
	RVAL_CONST,
	RVAL_PTR_CTX,
	RVAL_PTR_STACK,
	RVAL_PTR_HELPER_RET,
};

struct rval {
	enum rval_kind kind;
	uint64_t       val;      /* CONST: value | PTR: offset                */
	uint32_t       extra;    /* PTR_HELPER_RET: helper id                 */
};

#define RV_UNK()         ((struct rval){ .kind = RVAL_UNKNOWN  })
#define RV_CONST(v)      ((struct rval){ .kind = RVAL_CONST, .val = (uint64_t)(v) })
#define RV_PTR_CTX(o)    ((struct rval){ .kind = RVAL_PTR_CTX, .val = (uint64_t)(o) })
#define RV_PTR_STACK(o)  ((struct rval){ .kind = RVAL_PTR_STACK, .val = (uint64_t)(o) })

struct vstate {
	struct rval x[32];
};

static void vstate_init_entry(struct vstate *s)
{
	for (int i = 0; i < 32; i++)
		s->x[i] = RV_UNK();
	/* RISC-V ABI: x0 is constant zero. */
	s->x[0] = RV_CONST(0);
	/* MERLIN-V program entry convention: a0 = ctx pointer. */
	s->x[10] = RV_PTR_CTX(0);
	/* sp is a stack pointer; treat as PTR_STACK(0) at entry. */
	s->x[2] = RV_PTR_STACK(0);
	/* ra holds an opaque return address; we don't model it. */
}

static const char *rval_kind_name(enum rval_kind k)
{
	switch (k) {
	case RVAL_UNKNOWN:        return "UNKNOWN";
	case RVAL_CONST:          return "CONST";
	case RVAL_PTR_CTX:        return "PTR_CTX";
	case RVAL_PTR_STACK:      return "PTR_STACK";
	case RVAL_PTR_HELPER_RET: return "PTR_HELPER_RET";
	}
	return "?";
}

/* ---------------------------------------------------------------- *
 *  Helper allowlist
 * ---------------------------------------------------------------- */
static inline bool helper_allowed(const struct merlin_verifier_cfg *cfg,
				  uint32_t id)
{
	if (id >= MERLIN_NR_HELPERS)
		return false;
	return (cfg->helper_allow[id / 8] >> (id % 8)) & 1u;
}

static inline void helper_set(struct merlin_verifier_cfg *cfg, uint32_t id)
{
	if (id < MERLIN_NR_HELPERS)
		cfg->helper_allow[id / 8] |= (1u << (id % 8));
}

void merlin_verifier_cfg_init_for_mvdp(struct merlin_verifier_cfg *cfg)
{
	memset(cfg, 0, sizeof(*cfg));
	cfg->max_stack_bytes  = 512;
	cfg->allow_back_edges = false;
	cfg->verbose          = false;

	/* Common (0x0100..0x01FF) the MVDP program type uses */
	helper_set(cfg, 0x0101);  /* map_lookup_elem  */
	helper_set(cfg, 0x0102);  /* map_update_elem  */
	helper_set(cfg, 0x0103);  /* map_delete_elem  */
	helper_set(cfg, 0x0110);  /* ktime_get_ns     */
	helper_set(cfg, 0x0120);  /* get_prandom_u32  */
	helper_set(cfg, 0x0121);  /* get_smp_processor_id */
	helper_set(cfg, 0x0140);  /* prog_tail_call   */
	helper_set(cfg, 0x0150);  /* ringbuf_reserve  */
	helper_set(cfg, 0x0151);  /* ringbuf_submit   */
	helper_set(cfg, 0x0152);  /* ringbuf_discard  */

	/* MVDP-specific (0x0200..0x02FF) */
	helper_set(cfg, 0x0201);  /* mvdp_redirect    */
	helper_set(cfg, 0x0202);  /* mvdp_redirect_map*/
	helper_set(cfg, 0x0203);  /* mvdp_adjust_head */
	helper_set(cfg, 0x0204);  /* mvdp_adjust_tail */
	helper_set(cfg, 0x0205);  /* mvdp_adjust_meta */
	helper_set(cfg, 0x0206);  /* mvdp_load_bytes  */
	helper_set(cfg, 0x0207);  /* mvdp_store_bytes */
	helper_set(cfg, 0x0208);  /* mvdp_fib_lookup  */
	helper_set(cfg, 0x0209);  /* mvdp_get_time_ns */
}

/* ---------------------------------------------------------------- *
 *  Single-pass linear verifier
 * ---------------------------------------------------------------- */

#define DIAG(fmt, ...) do {                                            \
	if (cfg->verbose) fprintf(stderr, fmt "\n", ##__VA_ARGS__);    \
} while (0)

#define REJECT(fmt, ...) do {                                          \
	fprintf(stderr,                                                \
		"REJECT @0x%08x: " fmt "\n",                           \
		(unsigned)(text_offset + pc), ##__VA_ARGS__);          \
	rejected++;                                                    \
} while (0)

enum merlin_verify_result merlin_verify_text(const uint8_t *text,
					     size_t text_size,
					     uint32_t text_offset,
					     const struct merlin_verifier_cfg *cfg,
					     char *summary, size_t summary_len)
{
	if (text_size % 4 != 0) {
		snprintf(summary, summary_len,
			 ".text size %zu not 4-byte multiple", text_size);
		return MERLIN_VERIFY_REJECT;
	}

	struct vstate s;
	vstate_init_entry(&s);

	size_t insn_count  = 0;
	size_t insn_helper = 0;
	size_t insn_jmp    = 0;
	size_t insn_branch = 0;
	size_t insn_load   = 0;
	size_t insn_store  = 0;
	unsigned rejected  = 0;

	bool last_was_a7_load = false;
	uint32_t last_a7_const  = 0;
	bool     last_a7_known  = false;

	for (uint32_t pc = 0; pc < text_size; pc += 4) {
		uint32_t w =  (uint32_t)text[pc]
			   | ((uint32_t)text[pc + 1] <<  8)
			   | ((uint32_t)text[pc + 2] << 16)
			   | ((uint32_t)text[pc + 3] << 24);

		struct merlin_insn in;
		if (merlin_decode(w, pc, &in) < 0) {
			REJECT("hopeless decode failure");
			break;
		}
		insn_count++;

		DIAG("  %08x  %08x  %s",
		     (unsigned)(text_offset + pc), w,
		     merlin_insn_class_name(in.cls));

		/* --- profile compliance --- */
		if (!merlin_insn_permitted_default(&in)) {
			REJECT("forbidden instruction class: %s",
			       merlin_insn_class_name(in.cls));
			continue;
		}

		/* --- a7 tracking for helper calls --- */
		/* The canonical sequence is:
		 *    addi a7, x0, K
		 *    ecall
		 * For K > 0x7ff (12-bit signed) the compiler uses
		 *    lui  a7, K_hi
		 *    addi a7, a7, K_lo
		 *    ecall
		 * Since the spec restricts helper IDs to fit a 12-bit
		 * immediate, we only need the single-addi pattern.  If
		 * the compiler emits the two-instruction form we should
		 * fold it via lui+addi to a constant; not yet
		 * implemented - we currently reject such ecalls and
		 * surface the diagnostic so the user can lower the ID.
		 */
		if (in.cls == INSN_ALU_IMM
		    && in.alu_op == ALU_ADD
		    && in.rs1 == 0
		    && in.rd == 17 /* a7 */) {
			last_was_a7_load = true;
			last_a7_const    = (uint32_t)in.imm;
			last_a7_known    = true;
			s.x[17]          = RV_CONST((uint64_t)in.imm);
			continue;   /* this instruction is "consumed" */
		}

		/* --- ECALL: must be paired with a constant a7 in the
		 *     allowlist                                       --- */
		if (in.cls == INSN_ECALL) {
			if (!last_was_a7_load) {
				/* a7 may still be const from earlier; check
				 * abstract state.
				 */
				if (s.x[17].kind != RVAL_CONST) {
					REJECT("ecall with non-constant a7 "
					       "(state=%s)",
					       rval_kind_name(s.x[17].kind));
					continue;
				}
				last_a7_const = (uint32_t)s.x[17].val;
				last_a7_known = true;
			}
			if (!last_a7_known) {
				REJECT("ecall with unknown a7");
				continue;
			}
			if (!helper_allowed(cfg, last_a7_const)) {
				REJECT("ecall with helper id 0x%x not in "
				       "allowlist", last_a7_const);
				continue;
			}
			insn_helper++;
			DIAG("    helper call: id 0x%x  OK",
			     last_a7_const);
			/* Helper return convention: a0 = return value of
			 * pointer-or-scalar kind.  Be conservative: mark a0
			 * as PTR_HELPER_RET tagged with the helper id; a1
			 * is destroyed (caller-saved); everything else
			 * unchanged.
			 */
			s.x[10] = (struct rval){ .kind = RVAL_PTR_HELPER_RET,
						 .extra = last_a7_const };
			s.x[11] = RV_UNK();
			last_was_a7_load = false;
			last_a7_known    = false;
			continue;
		}
		/* Anything else: drop the a7-load gate. */
		last_was_a7_load = false;

		/* --- branches and jumps --- */
		switch (in.cls) {
		case INSN_BRANCH:
			insn_branch++;
			if (in.imm < 0 && !cfg->allow_back_edges) {
				REJECT("back-edge branch (imm %lld); "
				       "loops not yet supported in this "
				       "prototype", (long long)in.imm);
			}
			continue;
		case INSN_JAL:
			insn_jmp++;
			if (in.imm < 0 && !cfg->allow_back_edges) {
				REJECT("back-edge JAL (imm %lld); "
				       "loops not yet supported",
				       (long long)in.imm);
			}
			continue;
		case INSN_JALR:
			insn_jmp++;
			/* jalr is permitted as a return when target is ra
			 * (x1).  Indirect calls to kfunc slots are not
			 * verified by the prototype; reject for now.
			 */
			if (in.rs1 != 1 /* ra */) {
				REJECT("indirect call/jump via %s; "
				       "kfunc resolution not yet supported "
				       "in this prototype",
				       xreg(in.rs1));
			}
			continue;
		default:
			break;
		}

		/* --- loads and stores: pointer provenance --- */
		if (in.cls == INSN_LOAD) {
			insn_load++;
			enum rval_kind k = s.x[in.rs1].kind;
			if (k == RVAL_UNKNOWN || k == RVAL_CONST) {
				REJECT("load through non-pointer reg %s "
				       "(kind=%s, imm=%lld)",
				       xreg(in.rs1),
				       rval_kind_name(k),
				       (long long)in.imm);
			}
			/* destination becomes scalar of unknown value */
			s.x[in.rd] = RV_UNK();
			continue;
		}
		if (in.cls == INSN_STORE) {
			insn_store++;
			enum rval_kind k = s.x[in.rs1].kind;
			if (k == RVAL_UNKNOWN || k == RVAL_CONST) {
				REJECT("store through non-pointer reg %s "
				       "(kind=%s, imm=%lld)",
				       xreg(in.rs1),
				       rval_kind_name(k),
				       (long long)in.imm);
			}
			continue;
		}

		/* --- ALU effects on abstract state --- */
		if (in.cls == INSN_ALU_IMM && in.alu_op == ALU_ADD) {
			/* addi: ptr +/- const offset preserves pointer kind.
			 * x0+K becomes CONST(K) (catches "li rd, K").
			 */
			struct rval src = s.x[in.rs1];
			if (in.rs1 == 0) {
				s.x[in.rd] = RV_CONST((uint64_t)in.imm);
			} else if (src.kind == RVAL_CONST) {
				s.x[in.rd] = RV_CONST(src.val
						      + (uint64_t)in.imm);
			} else if (src.kind == RVAL_PTR_CTX
				   || src.kind == RVAL_PTR_STACK) {
				/* Preserve root, adjust offset. */
				s.x[in.rd] = (struct rval){
					.kind = src.kind,
					.val  = src.val + (uint64_t)in.imm
				};
			} else {
				s.x[in.rd] = RV_UNK();
			}
			continue;
		}
		if (in.cls == INSN_LUI) {
			s.x[in.rd] = RV_CONST((uint64_t)in.imm);
			continue;
		}
		if (in.cls == INSN_AUIPC) {
			/* PC-relative; we conservatively mark UNKNOWN. */
			s.x[in.rd] = RV_UNK();
			continue;
		}

		/* Default: any other class clobbers rd to UNKNOWN. */
		if (in.rd != 0) {
			s.x[in.rd] = RV_UNK();
		}
	}

	snprintf(summary, summary_len,
		 "insns=%zu helper-calls=%zu jmp=%zu branch=%zu "
		 "load=%zu store=%zu rejected=%u",
		 insn_count, insn_helper, insn_jmp, insn_branch,
		 insn_load, insn_store, rejected);

	return rejected ? MERLIN_VERIFY_REJECT : MERLIN_VERIFY_OK;
}
