// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * verify.c - Phase-2 abstract-interpretation verifier.
 *
 * Implementation strategy:
 *
 *   1. Build the CFG (cfg.c).
 *   2. Compute reverse postorder; build the initial worklist as all
 *      reachable blocks in RPO.
 *   3. Maintain entry_state[b] (the abstract state at the entry of
 *      block b) for every block.  Bottom == "not yet computed".
 *   4. While the worklist is non-empty:
 *        - pop block b
 *        - simulate b starting from entry_state[b], producing
 *          exit_state for each successor edge
 *        - for each successor s, join exit_state into entry_state[s]
 *          and push s if the join changed entry_state[s]
 *      Step-cap guards against pathological non-termination on
 *      irreducible CFGs.
 *   5. After fixpoint, reject if any per-instruction safety property
 *      failed.
 *
 * Bounded loops (merlin_loop_bound):
 *
 *   The verifier maintains a "back_edge_ok[b]" bit for each block b
 *   that is a loop header.  When the canonical sequence
 *      addi a7, x0, 0x142
 *      addi a0, x0, K          (K constant)
 *      ecall
 *   is seen, the very-next-block-by-fall-through is marked
 *   back_edge_ok.  A back edge whose destination has back_edge_ok set
 *   is accepted; otherwise it is rejected.
 *
 *   This is intentionally syntactic and conservative: the canonical
 *   sequence is what GCC emits for the C macro
 *      #define MERLIN_LOOP(K)  __merlin_helper_call(0x142, K)
 *   and the runtime separately enforces the dynamic trip cap.
 */
#include "verify.h"
#include "cfg.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MERLIN_VERIFY_STEP_CAP_DEFAULT (1u << 20) /* 1 M transfers */

/* ABI register names for diagnostics. */
static const char *xreg(unsigned r)
{
	static const char *names[32] = {
		"zero",	 "ra", "sp",  "gp",  "tp", "t0", "t1", "t2",
		"s0/fp", "s1", "a0",  "a1",  "a2", "a3", "a4", "a5",
		"a6",	 "a7", "s2",  "s3",  "s4", "s5", "s6", "s7",
		"s8",	 "s9", "s10", "s11", "t3", "t4", "t5", "t6",
	};
	return r < 32 ? names[r] : "?";
}

static const char *rval_kind_name(enum merlin_rval_kind k)
{
	switch (k) {
	case RVAL_UNINIT:
		return "Uninit";
	case RVAL_SCALAR:
		return "Scalar";
	case RVAL_PTR_CTX:
		return "PtrCtx";
	case RVAL_PTR_STACK:
		return "PtrStack";
	case RVAL_PTR_HELPER_RET:
		return "PtrHelperRet";
	}
	return "?";
}

/* ---------- rval constructors ---------- */
static struct merlin_rval rv_uninit(void)
{
	return (struct merlin_rval){ .kind = RVAL_UNINIT };
}
static struct merlin_rval rv_scalar(struct merlin_scalar s)
{
	return (struct merlin_rval){ .kind = RVAL_SCALAR, .s = s };
}
static struct merlin_rval rv_const(int64_t v)
{
	return rv_scalar(scalar_const(v));
}
static struct merlin_rval rv_scalar_top(void)
{
	return rv_scalar(scalar_top());
}
static struct merlin_rval rv_ptr(enum merlin_rval_kind k, int64_t off_min,
				 int64_t off_max)
{
	return (struct merlin_rval){
		.kind = k,
		.off_min = off_min,
		.off_max = off_max,
	};
}

/* ---------- vstate ---------- */
static void vstate_init_entry(struct merlin_vstate *s)
{
	int i;
	for (i = 0; i < 32; i++)
		s->x[i] = rv_uninit();
	s->x[0] = rv_const(0); /* x0 == 0           */
	s->x[10] = rv_ptr(RVAL_PTR_CTX, 0, 0); /* a0 = ctx          */
	s->x[2] = rv_ptr(RVAL_PTR_STACK, 0, 0); /* sp = stack base */
	s->valid = true;
}

static struct merlin_rval rval_join(struct merlin_rval a, struct merlin_rval b)
{
	if (a.kind == RVAL_UNINIT)
		return b;
	if (b.kind == RVAL_UNINIT)
		return a;
	if (a.kind != b.kind) {
		/* Type mismatch across paths -> scalar top is the
 * safest meet that preserves soundness.
 */
		return rv_scalar_top();
	}
	if (a.kind == RVAL_SCALAR) {
		return rv_scalar(scalar_join(a.s, b.s));
	}
	if (a.kind == RVAL_PTR_HELPER_RET) {
		if (a.helper_id != b.helper_id)
			return rv_scalar_top();
		return a;
	}
	/* Pointer kinds: widen offset range. */
	{
		struct merlin_rval r = a;
		r.off_min = a.off_min < b.off_min ? a.off_min : b.off_min;
		r.off_max = a.off_max > b.off_max ? a.off_max : b.off_max;
		return r;
	}
}

static bool rval_equal(struct merlin_rval a, struct merlin_rval b)
{
	if (a.kind != b.kind)
		return false;
	switch (a.kind) {
	case RVAL_UNINIT:
		return true;
	case RVAL_SCALAR:
		return scalar_equal(a.s, b.s);
	case RVAL_PTR_HELPER_RET:
		return a.helper_id == b.helper_id;
	case RVAL_PTR_CTX:
	case RVAL_PTR_STACK:
		return a.off_min == b.off_min && a.off_max == b.off_max;
	}
	return false;
}

static bool vstate_join(struct merlin_vstate *dst,
			const struct merlin_vstate *src)
{
	bool changed = false;
	int i;
	if (!src->valid)
		return false;
	if (!dst->valid) {
		*dst = *src;
		return true;
	}
	for (i = 0; i < 32; i++) {
		struct merlin_rval nv = rval_join(dst->x[i], src->x[i]);
		if (!rval_equal(nv, dst->x[i])) {
			dst->x[i] = nv;
			changed = true;
		}
	}
	return changed;
}

/* ---------- helper allowlist ---------- */
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
	cfg->max_stack_bytes = 512;
	cfg->allow_back_edges = true; /* Phase 2: gated on bound  */
	cfg->verbose = false;
	cfg->step_cap = MERLIN_VERIFY_STEP_CAP_DEFAULT;

	helper_set(cfg, 0x0101); /* map_lookup_elem  */
	helper_set(cfg, 0x0102); /* map_update_elem  */
	helper_set(cfg, 0x0103); /* map_delete_elem  */
	helper_set(cfg, 0x0110); /* ktime_get_ns     */
	helper_set(cfg, 0x0120); /* get_prandom_u32  */
	helper_set(cfg, 0x0121); /* get_smp_processor_id */
	helper_set(cfg, 0x0140); /* prog_tail_call   */
	helper_set(cfg, MERLIN_HELPER_LOOP_BOUND); /* 0x0142 */
	helper_set(cfg, 0x0150); /* ringbuf_reserve  */
	helper_set(cfg, 0x0151); /* ringbuf_submit   */
	helper_set(cfg, 0x0152); /* ringbuf_discard  */
	helper_set(cfg, 0x0201); /* mvdp_redirect    */
	helper_set(cfg, 0x0202); /* mvdp_redirect_map*/
	helper_set(cfg, 0x0203); /* mvdp_adjust_head */
	helper_set(cfg, 0x0204); /* mvdp_adjust_tail */
	helper_set(cfg, 0x0205); /* mvdp_adjust_meta */
	helper_set(cfg, 0x0206); /* mvdp_load_bytes  */
	helper_set(cfg, 0x0207); /* mvdp_store_bytes */
	helper_set(cfg, 0x0208); /* mvdp_fib_lookup  */
	helper_set(cfg, 0x0209); /* mvdp_get_time_ns */
}

/* ---------- transfer functions for a single instruction --------------- */

#define REJECT(fmt, ...)                                     \
	do {                                                 \
		fprintf(stderr, "REJECT @0x%08x: " fmt "\n", \
			(unsigned)(in.pc), ##__VA_ARGS__);   \
		*rejected += 1;                              \
	} while (0)

#define DIAG(fmt, ...)                                                 \
	do {                                                           \
		if (cfg->verbose)                                      \
			fprintf(stderr, "  " fmt "\n", ##__VA_ARGS__); \
	} while (0)

static bool ptr_kind(enum merlin_rval_kind k)
{
	return k == RVAL_PTR_CTX || k == RVAL_PTR_STACK ||
	       k == RVAL_PTR_HELPER_RET;
}

/* Width in bytes of a load/store, derived from the decoded kind. */
static unsigned ld_width(enum merlin_load_kind k)
{
	switch (k) {
	case LD_LB:
	case LD_LBU: return 1;
	case LD_LH:
	case LD_LHU: return 2;
	case LD_LW:
	case LD_LWU: return 4;
	case LD_LD:  return 8;
	}
	return 0;
}
static unsigned st_width(enum merlin_store_kind k)
{
	switch (k) {
	case ST_SB: return 1;
	case ST_SH: return 2;
	case ST_SW: return 4;
	case ST_SD: return 8;
	}
	return 0;
}

/*
 * step_insn - apply transfer function for one decoded instruction.
 * Updates *st in place.  Sets *rejected if a safety check fails.
 * Returns true to continue, false to stop the block (terminator).
 */
static void step_insn(struct merlin_insn in, struct merlin_vstate *st,
		      const struct merlin_verifier_cfg *cfg, unsigned *rejected,
		      uint32_t *pending_loop_bound)
{
	struct merlin_rval a7 = st->x[17];
	struct merlin_rval a0;

	/* Profile compliance. */
	if (!merlin_insn_permitted_default(&in)) {
		REJECT("forbidden instruction class: %s",
		       merlin_insn_class_name(in.cls));
		return;
	}

	/* ECALL: helper id from a7 (must be const + allowlisted). */
	if (in.cls == INSN_ECALL) {
		if (a7.kind != RVAL_SCALAR || !scalar_is_const(a7.s)) {
			REJECT("ecall with non-constant a7 (kind=%s)",
			       rval_kind_name(a7.kind));
			st->x[10] = rv_scalar_top();
			return;
		}
		{
			uint32_t hid = (uint32_t)a7.s.tn.value;
			if (!helper_allowed(cfg, hid)) {
				REJECT("ecall helper id 0x%x not in "
				       "allowlist",
				       hid);
				st->x[10] = rv_scalar_top();
				return;
			}
			DIAG("helper id 0x%x OK", hid);
			if (hid == MERLIN_HELPER_LOOP_BOUND) {
				/* Mark the next block as a permitted loop
 * header.  The trip count is in a0; if a0
 * is constant, record it for diagnostics.
 */
				a0 = st->x[10];
				if (a0.kind == RVAL_SCALAR &&
				    scalar_is_const(a0.s)) {
					*pending_loop_bound =
						(uint32_t)a0.s.tn.value;
				} else {
					*pending_loop_bound = 0xffffffff;
				}
			}
			/* Helper return convention: a0 = helper return,
 * caller-saved regs clobbered.
 */
			st->x[10] = (struct merlin_rval){
				.kind = RVAL_PTR_HELPER_RET,
				.helper_id = hid,
			};
			{
				int r;
				for (r = 11; r <= 17; r++)
					st->x[r] = rv_uninit();
				for (r = 5; r <= 7; r++)
					st->x[r] = rv_uninit();
				for (r = 28; r <= 31; r++)
					st->x[r] = rv_uninit();
			}
		}
		return;
	}

	/* LOAD: address rs1 must be a pointer; rd becomes scalar top. */
	if (in.cls == INSN_LOAD) {
		enum merlin_rval_kind k = st->x[in.rs1].kind;
		if (!ptr_kind(k)) {
			REJECT("load through non-pointer reg %s (kind=%s)",
			       xreg(in.rs1), rval_kind_name(k));
		} else if (k == RVAL_PTR_STACK) {
			/* Stack-discipline: the entire access window
			 * must lie inside the declared frame budget.
			 *
			 *   abs_off = base.off + imm
			 *   width   = 1 / 2 / 4 / 8
			 *
			 *   require:  -max_stack_bytes <= abs_off
			 *             abs_off + width <= 0
			 *
			 * Both endpoints of base.off_min..base.off_max
			 * are checked.
			 */
			int64_t lo = st->x[in.rs1].off_min + in.imm;
			int64_t hi = st->x[in.rs1].off_max + in.imm;
			unsigned w = ld_width(in.ld_kind);
			int64_t budget = (int64_t)cfg->max_stack_bytes;
			if (lo < -budget || (hi + (int64_t)w) > 0) {
				REJECT("stack load out of frame "
				       "(off=[%lld,%lld]+%u, budget=%lld)",
				       (long long)lo, (long long)hi, w,
				       (long long)budget);
			}
		}
		if (in.rd == 2) {
			REJECT("load into sp (sp may only be set by "
			       "addi sp, sp, K)");
			st->x[2] = rv_scalar_top();
			return;
		}
		if (in.rd != 0)
			st->x[in.rd] = rv_scalar_top();
		return;
	}
	if (in.cls == INSN_STORE) {
		enum merlin_rval_kind k = st->x[in.rs1].kind;
		if (!ptr_kind(k)) {
			REJECT("store through non-pointer reg %s (kind=%s)",
			       xreg(in.rs1), rval_kind_name(k));
		} else if (k == RVAL_PTR_STACK) {
			int64_t lo = st->x[in.rs1].off_min + in.imm;
			int64_t hi = st->x[in.rs1].off_max + in.imm;
			unsigned w = st_width(in.st_kind);
			int64_t budget = (int64_t)cfg->max_stack_bytes;
			if (lo < -budget || (hi + (int64_t)w) > 0) {
				REJECT("stack store out of frame "
				       "(off=[%lld,%lld]+%u, budget=%lld)",
				       (long long)lo, (long long)hi, w,
				       (long long)budget);
			}
		}
		return;
	}

	/* ALU IMM: tracked precisely for ADD; conservatively for others. */
	if (in.cls == INSN_ALU_IMM) {
		struct merlin_rval src = st->x[in.rs1];
		if (in.alu_op == ALU_ADD) {
			if (in.rs1 == 0) {
				if (in.rd == 2) {
					REJECT("write to sp from non-sp "
					       "source (only addi sp, sp, K "
					       "permitted)");
					st->x[2] = rv_scalar_top();
					return;
				}
				if (in.rd != 0)
					st->x[in.rd] = rv_const(in.imm);
				return;
			}
			/* Stack-discipline: sp may only be updated via
			 * addi sp, sp, K (rs1 must already be sp).
			 */
			if (in.rd == 2 && in.rs1 != 2) {
				REJECT("write to sp from non-sp source %s",
				       xreg(in.rs1));
				st->x[2] = rv_scalar_top();
				return;
			}
			if (src.kind == RVAL_SCALAR) {
				struct merlin_scalar k = scalar_const(in.imm);
				if (in.rd == 2) {
					REJECT("addi sp, sp, K applied to "
					       "scalar-kind sp");
					st->x[2] = rv_scalar_top();
					return;
				}
				if (in.rd != 0)
					st->x[in.rd] =
						rv_scalar(scalar_add(src.s, k));
				return;
			}
			if (src.kind == RVAL_PTR_CTX ||
			    src.kind == RVAL_PTR_STACK) {
				if (in.rd != 0) {
					struct merlin_rval r = src;
					r.off_min += in.imm;
					r.off_max += in.imm;
					if (in.rd == 2 &&
					    src.kind == RVAL_PTR_STACK) {
						int64_t budget =
							(int64_t)cfg
								->max_stack_bytes;
						if (r.off_min < -budget) {
							REJECT("sp underflow: "
							       "new sp.off=%lld "
							       "below budget %lld",
							       (long long)
								       r.off_min,
							       (long long)
								       budget);
						}
						if (r.off_max > 0) {
							REJECT("sp above entry: "
							       "new sp.off=%lld",
							       (long long)
								       r.off_max);
						}
					}
					st->x[in.rd] = r;
				}
				return;
			}
			if (in.rd == 2) {
				REJECT("addi sp, ?, K from non-pointer src "
				       "(kind=%s)",
				       rval_kind_name(src.kind));
				st->x[2] = rv_scalar_top();
				return;
			}
			if (in.rd != 0)
				st->x[in.rd] = rv_scalar_top();
			return;
		}
		/* Other ALU-imm (not ADD): result is a scalar
		 * (loses pointer kind).
		 */
		if (in.rd == 2) {
			REJECT("non-add ALU writing to sp");
			st->x[2] = rv_scalar_top();
			return;
		}
		if (in.rd != 0)
			st->x[in.rd] = rv_scalar_top();
		return;
	}
	if (in.cls == INSN_LUI) {
		if (in.rd == 2) {
			REJECT("lui sp clobbers stack pointer");
			st->x[2] = rv_scalar_top();
			return;
		}
		if (in.rd != 0)
			st->x[in.rd] = rv_const(in.imm);
		return;
	}
	if (in.cls == INSN_AUIPC) {
		if (in.rd == 2) {
			REJECT("auipc sp clobbers stack pointer");
			st->x[2] = rv_scalar_top();
			return;
		}
		if (in.rd != 0)
			st->x[in.rd] = rv_scalar_top();
		return;
	}
	if (in.cls == INSN_ALU_REG) {
		/* Pointer arithmetic on two unconstrained values is
		 * unsound: reject.  Otherwise: scalar top.
		 */
		bool p1 = ptr_kind(st->x[in.rs1].kind);
		bool p2 = ptr_kind(st->x[in.rs2].kind);
		if (p1 && p2) {
			REJECT("ALU on two pointers (%s, %s)", xreg(in.rs1),
			       xreg(in.rs2));
		}
		if (in.rd == 2) {
			REJECT("reg-ALU writing to sp (only addi sp, sp, K "
			       "permitted)");
			st->x[2] = rv_scalar_top();
			return;
		}
		if (in.rd != 0) {
			if (p1 || p2) {
				/* ptr +/- scalar -> propagate ptr with
				 * a widened offset range.
				 */
				struct merlin_rval ptr = p1 ? st->x[in.rs1] :
							      st->x[in.rs2];
				ptr.off_min = -(1ll << 30);
				ptr.off_max = (1ll << 30);
				st->x[in.rd] = ptr;
			} else {
				st->x[in.rd] = rv_scalar_top();
			}
		}
		return;
	}

	/* Branches / jumps: terminator; CFG handles successors. */
	if (in.cls == INSN_BRANCH || in.cls == INSN_JAL ||
	    in.cls == INSN_JALR) {
		if (in.cls == INSN_JAL && in.rd == 2) {
			REJECT("JAL writing return address into sp");
			st->x[2] = rv_scalar_top();
			return;
		}
		if (in.cls == INSN_JAL && in.rd != 0)
			st->x[in.rd] = rv_scalar_top();
		return;
	}

	/* Default: clobber rd to scalar top. */
	if (in.rd != 0)
		st->x[in.rd] = rv_scalar_top();
}

/* ---------- block-level driver ---------------------------------------- */

struct ws {
	int items[MERLIN_CFG_MAX_BLOCKS];
	int head;
};
static void ws_push(struct ws *q, int b)
{
	int i;
	for (i = 0; i < q->head; i++)
		if (q->items[i] == b)
			return;
	q->items[q->head++] = b;
}
static int ws_pop(struct ws *q)
{
	if (q->head == 0)
		return -1;
	return q->items[--q->head];
}

enum merlin_verify_result
merlin_verify_text(const uint8_t *text, size_t text_size, uint32_t text_offset,
		   const struct merlin_verifier_cfg *cfg, char *summary,
		   size_t summary_len)
{
	struct merlin_cfg G;
	struct merlin_vstate *entry_state;
	bool *loop_header_ok;
	struct ws q = { 0 };
	uint32_t steps = 0;
	unsigned rejected = 0;
	size_t insn_count = 0, insn_helper = 0, insn_jmp = 0;
	size_t insn_branch = 0, insn_load = 0, insn_store = 0;
	uint32_t step_cap = cfg->step_cap ? cfg->step_cap :
					    MERLIN_VERIFY_STEP_CAP_DEFAULT;
	int rc, i;

	if (text_size % 4 != 0) {
		snprintf(summary, summary_len,
			 ".text size %zu not 4-byte multiple", text_size);
		return MERLIN_VERIFY_REJECT;
	}

	rc = merlin_cfg_build(text, text_size, text_offset, &G);
	if (rc < 0) {
		snprintf(summary, summary_len, "CFG build failed: %s",
			 G.reject_reason ? G.reject_reason : "unknown");
		return MERLIN_VERIFY_REJECT;
	}

	entry_state = calloc(G.nblocks, sizeof(*entry_state));
	loop_header_ok = calloc(G.nblocks, sizeof(*loop_header_ok));
	if (!entry_state || !loop_header_ok) {
		free(entry_state);
		free(loop_header_ok);
		snprintf(summary, summary_len, "out of memory");
		return MERLIN_VERIFY_REJECT;
	}

	/* Entry: block 0 starts with the canonical entry state. */
	vstate_init_entry(&entry_state[0]);
	ws_push(&q, 0);

	while ((i = ws_pop(&q)) >= 0) {
		struct merlin_block *b = &G.blocks[i];
		struct merlin_vstate st = entry_state[i];
		uint32_t pending_loop_bound = 0;
		uint32_t off;
		int s;

		if (!st.valid)
			continue;

		for (off = b->start_pc - text_offset;
		     off <= b->end_pc - text_offset && off < text_size;
		     off += 4) {
			uint32_t w = (uint32_t)text[off] |
				     ((uint32_t)text[off + 1] << 8) |
				     ((uint32_t)text[off + 2] << 16) |
				     ((uint32_t)text[off + 3] << 24);
			struct merlin_insn in;

			if (merlin_decode(w, text_offset + off, &in) < 0) {
				fprintf(stderr,
					"REJECT @0x%08x: undecipherable\n",
					text_offset + off);
				rejected++;
				continue;
			}
			insn_count++;
			switch (in.cls) {
			case INSN_BRANCH:
				insn_branch++;
				break;
			case INSN_JAL:
			case INSN_JALR:
				insn_jmp++;
				break;
			case INSN_LOAD:
				insn_load++;
				break;
			case INSN_STORE:
				insn_store++;
				break;
			case INSN_ECALL:
				insn_helper++;
				break;
			default:
				break;
			}

			steps++;
			if (steps > step_cap) {
				snprintf(summary, summary_len,
					 "step cap %u exceeded "
					 "(possible irreducible CFG)",
					 step_cap);
				free(entry_state);
				free(loop_header_ok);
				return MERLIN_VERIFY_REJECT;
			}

			step_insn(in, &st, cfg, &rejected, &pending_loop_bound);
		}

		/* Apply any pending loop-bound annotation: the immediate
 * fall-through block is permitted as a loop header.
 */
		if (pending_loop_bound != 0 && b->succ[0] >= 0)
			loop_header_ok[b->succ[0]] = true;

		/* Push successor entries with the joined state. */
		for (s = 0; s < 2; s++) {
			int n = b->succ[s];
			if (n < 0)
				continue;
			if (vstate_join(&entry_state[n], &st))
				ws_push(&q, n);
		}
	}

	/* Back-edge gating: every back edge whose destination is not a
 * marked loop header is a hard reject.
 */
	for (i = 0; i < (int)G.back_edge_count; i++) {
		int src = G.back_edges[i].src;
		int dst = G.back_edges[i].dst;
		if (!loop_header_ok[dst]) {
			fprintf(stderr,
				"REJECT: unguarded back edge "
				"%d -> %d (no merlin_loop_bound)\n",
				src, dst);
			rejected++;
		}
	}

	snprintf(summary, summary_len,
		 "blocks=%u back_edges=%u steps=%u "
		 "insns=%zu helper=%zu jmp=%zu branch=%zu "
		 "load=%zu store=%zu rejected=%u",
		 G.nblocks, G.back_edge_count, steps, insn_count, insn_helper,
		 insn_jmp, insn_branch, insn_load, insn_store, rejected);

	free(entry_state);
	free(loop_header_ok);
	return rejected ? MERLIN_VERIFY_REJECT : MERLIN_VERIFY_OK;
}
