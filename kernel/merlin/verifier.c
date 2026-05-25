// SPDX-License-Identifier: GPL-2.0-only
/*
 * verifier.c - MERLIN-V Phase-2 abstract-interpretation verifier
 *              (kernel port of tools/merlin-verifier/verify.c).
 *
 * Phase-2 (this file) adds, over the linear-pass Phase-1 verifier:
 *
 *   - CFG construction with reducibility check (cfg.c).
 *   - Worklist abstract interpretation over basic blocks.
 *   - State joins at block entry (per-register tnum + range merge).
 *   - Scalar range tracking (signed + unsigned + tnum mask).
 *   - Pointer offset bounds (off_min..off_max) per typed root.
 *   - merlin_loop_bound() helper recognition for bounded loops.
 *
 * Per docs/design/06-verifier.md the verifier domain mirrors the
 * eBPF verifier's abstract domain (tnum + signed/unsigned range +
 * pointer provenance + offset bounds) so that the eventual
 * factoring into a shared kernel module (Option A) is a syntactic
 * change rather than a semantic one.
 *
 * Memory discipline: the worklist allocates entry_state[] /
 * loop_header_ok[] / queue[] from the slab once; everything else is
 * stack-local.  No GFP_ATOMIC: the verifier runs only from the
 * MERLIN_PROG_LOAD syscall path, which is sleepable.
 */

#include <linux/limits.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/types.h>

#include "include/merlin_internal.h"

#define MERLIN_VERIFY_STEP_CAP_DEFAULT (1u << 20)

/* ----- diagnostics ----- */
static const char *const xreg_name[32] = {
	"zero", "ra", "sp", "gp", "tp",	 "t0",	"t1", "t2", "s0/fp", "s1", "a0",
	"a1",	"a2", "a3", "a4", "a5",	 "a6",	"a7", "s2", "s3",    "s4", "s5",
	"s6",	"s7", "s8", "s9", "s10", "s11", "t3", "t4", "t5",    "t6",
};
static inline const char *xreg(unsigned r)
{
	return r < 32 ? xreg_name[r] : "?";
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

/* ----- rval constructors ----- */
static struct merlin_rval rv_uninit(void)
{
	return (struct merlin_rval){ .kind = RVAL_UNINIT };
}
static struct merlin_rval rv_scalar(struct merlin_scalar s)
{
	return (struct merlin_rval){ .kind = RVAL_SCALAR, .s = s };
}
static struct merlin_rval rv_const(s64 v)
{
	return rv_scalar(scalar_const(v));
}
static struct merlin_rval rv_scalar_top(void)
{
	return rv_scalar(scalar_top());
}
static struct merlin_rval rv_ptr(enum merlin_rval_kind k, s64 lo, s64 hi)
{
	return (struct merlin_rval){ .kind = k, .off_min = lo, .off_max = hi };
}

static void vstate_init_entry(struct merlin_vstate *s)
{
	int i;
	for (i = 0; i < 32; i++)
		s->x[i] = rv_uninit();
	s->x[0] = rv_const(0);
	s->x[10] = rv_ptr(RVAL_PTR_CTX, 0, 0);
	s->x[2] = rv_ptr(RVAL_PTR_STACK, 0, 0);
	s->valid = true;
}

static struct merlin_rval rval_join(struct merlin_rval a, struct merlin_rval b)
{
	if (a.kind == RVAL_UNINIT)
		return b;
	if (b.kind == RVAL_UNINIT)
		return a;
	if (a.kind != b.kind)
		return rv_scalar_top();
	if (a.kind == RVAL_SCALAR)
		return rv_scalar(scalar_join(a.s, b.s));
	if (a.kind == RVAL_PTR_HELPER_RET) {
		if (a.helper_id != b.helper_id)
			return rv_scalar_top();
		return a;
	}
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

/* ----- helper allowlist ----- */
static inline bool helper_allowed(const struct merlin_verifier_cfg *cfg, u32 id)
{
	if (id > MERLIN_MAX_HELPER_ID)
		return false;
	return !!(cfg->helper_allow[id / 8] & (1u << (id % 8)));
}

#define REJECT(in, fmt, ...)                                                  \
	do {                                                                  \
		rejected++;                                                   \
		merlin_log((struct merlin_verifier_cfg *)(cfg),               \
			   "pc=%u: REJECT: " fmt "\n", in.pc, ##__VA_ARGS__); \
	} while (0)

#define VTRACE(in, fmt, ...)                                            \
	do {                                                            \
		if (cfg->verbose)                                       \
			merlin_log((struct merlin_verifier_cfg *)(cfg), \
				   "  pc=%u: " fmt "\n", in.pc,         \
				   ##__VA_ARGS__);                      \
	} while (0)

static bool ptr_kind(enum merlin_rval_kind k)
{
	return k == RVAL_PTR_CTX || k == RVAL_PTR_STACK ||
	       k == RVAL_PTR_HELPER_RET;
}

/* Transfer function for one decoded instruction (in-place state). */
static void step_insn(struct merlin_insn in, struct merlin_vstate *st,
		      const struct merlin_verifier_cfg *cfg, u32 *rejected_ptr,
		      u32 *pending_loop_bound)
{
	u32 rejected = *rejected_ptr;
	struct merlin_rval a7 = st->x[17];
	struct merlin_rval a0;

	if (!merlin_insn_permitted_default(&in)) {
		REJECT(in, "forbidden instruction class %s",
		       merlin_insn_class_name(in.cls));
		goto out;
	}

	if (in.cls == INSN_ECALL) {
		u32 hid;
		int r;

		if (a7.kind != RVAL_SCALAR || !scalar_is_const(a7.s)) {
			REJECT(in, "ecall with non-constant a7 (kind=%s)",
			       rval_kind_name(a7.kind));
			st->x[10] = rv_scalar_top();
			goto out;
		}
		hid = (u32)a7.s.tn.value;
		if (!helper_allowed(cfg, hid)) {
			REJECT(in, "ecall helper id 0x%x not in allowlist",
			       hid);
			st->x[10] = rv_scalar_top();
			goto out;
		}
		VTRACE(in, "helper id 0x%x OK", hid);
		if (hid == MERLIN_HELPER_LOOP_BOUND) {
			a0 = st->x[10];
			*pending_loop_bound = (a0.kind == RVAL_SCALAR &&
					       scalar_is_const(a0.s)) ?
						      (u32)a0.s.tn.value :
						      0xffffffffu;
		}
		st->x[10] = (struct merlin_rval){
			.kind = RVAL_PTR_HELPER_RET,
			.helper_id = hid,
		};
		for (r = 11; r <= 17; r++)
			st->x[r] = rv_uninit();
		for (r = 5; r <= 7; r++)
			st->x[r] = rv_uninit();
		for (r = 28; r <= 31; r++)
			st->x[r] = rv_uninit();
		goto out;
	}

	if (in.cls == INSN_LOAD) {
		enum merlin_rval_kind k = st->x[in.rs1].kind;
		if (!ptr_kind(k))
			REJECT(in, "load through non-pointer reg %s (kind=%s)",
			       xreg(in.rs1), rval_kind_name(k));
		if (in.rd != 0)
			st->x[in.rd] = rv_scalar_top();
		goto out;
	}
	if (in.cls == INSN_STORE) {
		enum merlin_rval_kind k = st->x[in.rs1].kind;
		if (!ptr_kind(k))
			REJECT(in, "store through non-pointer reg %s (kind=%s)",
			       xreg(in.rs1), rval_kind_name(k));
		goto out;
	}

	if (in.cls == INSN_ALU_IMM) {
		struct merlin_rval src = st->x[in.rs1];
		if (in.alu_op == ALU_ADD) {
			if (in.rs1 == 0) {
				if (in.rd != 0)
					st->x[in.rd] = rv_const(in.imm);
				goto out;
			}
			if (src.kind == RVAL_SCALAR) {
				struct merlin_scalar k = scalar_const(in.imm);
				if (in.rd != 0)
					st->x[in.rd] =
						rv_scalar(scalar_add(src.s, k));
				goto out;
			}
			if (src.kind == RVAL_PTR_CTX ||
			    src.kind == RVAL_PTR_STACK) {
				if (in.rd != 0) {
					struct merlin_rval r = src;
					r.off_min += in.imm;
					r.off_max += in.imm;
					st->x[in.rd] = r;
				}
				goto out;
			}
			if (in.rd != 0)
				st->x[in.rd] = rv_scalar_top();
			goto out;
		}
		if (in.rd != 0)
			st->x[in.rd] = rv_scalar_top();
		goto out;
	}
	if (in.cls == INSN_LUI) {
		if (in.rd != 0)
			st->x[in.rd] = rv_const(in.imm);
		goto out;
	}
	if (in.cls == INSN_AUIPC) {
		if (in.rd != 0)
			st->x[in.rd] = rv_scalar_top();
		goto out;
	}
	if (in.cls == INSN_ALU_REG) {
		bool p1 = ptr_kind(st->x[in.rs1].kind);
		bool p2 = ptr_kind(st->x[in.rs2].kind);
		if (p1 && p2)
			REJECT(in, "ALU on two pointers (%s, %s)", xreg(in.rs1),
			       xreg(in.rs2));
		if (in.rd != 0) {
			if (p1 || p2) {
				struct merlin_rval ptr = p1 ? st->x[in.rs1] :
							      st->x[in.rs2];
				ptr.off_min = -(1LL << 30);
				ptr.off_max = (1LL << 30);
				st->x[in.rd] = ptr;
			} else {
				st->x[in.rd] = rv_scalar_top();
			}
		}
		goto out;
	}

	if (in.cls == INSN_BRANCH || in.cls == INSN_JAL ||
	    in.cls == INSN_JALR) {
		if (in.cls == INSN_JAL && in.rd != 0)
			st->x[in.rd] = rv_scalar_top();
		goto out;
	}

	if (in.rd != 0)
		st->x[in.rd] = rv_scalar_top();
out:
	*rejected_ptr = rejected;
}

/* ----- worklist driver ----- */
struct merlin_ws {
	int *items;
	int head;
	int cap;
};
static void ws_push(struct merlin_ws *q, int b)
{
	int i;
	for (i = 0; i < q->head; i++)
		if (q->items[i] == b)
			return;
	if (q->head < q->cap)
		q->items[q->head++] = b;
}
static int ws_pop(struct merlin_ws *q)
{
	if (q->head == 0)
		return -1;
	return q->items[--q->head];
}

enum merlin_verify_result
merlin_verify_text(const u8 *text, size_t text_size, u32 text_offset,
		   const struct merlin_verifier_cfg *cfg)
{
	struct merlin_cfg *G = NULL;
	struct merlin_vstate *entry_state = NULL;
	struct merlin_vstate *scratch = NULL;
	bool *loop_header_ok = NULL;
	struct merlin_ws q = { 0 };
	int *ws_items = NULL;
	u32 steps = 0, rejected = 0;
	size_t insn_count = 0, insn_helper = 0, insn_jmp = 0;
	size_t insn_branch = 0, insn_load = 0, insn_store = 0;
	u32 step_cap = cfg->step_cap ? cfg->step_cap :
				       MERLIN_VERIFY_STEP_CAP_DEFAULT;
	int rc, i;
	enum merlin_verify_result result = MERLIN_VERIFY_REJECT;

	if (text_size % 4 != 0) {
		merlin_log((struct merlin_verifier_cfg *)cfg,
			   "REJECT: text size %zu is not a multiple of 4\n",
			   text_size);
		return MERLIN_VERIFY_REJECT;
	}

	G = kzalloc(sizeof(*G), GFP_KERNEL);
	scratch = kzalloc(sizeof(*scratch), GFP_KERNEL);
	if (!G || !scratch)
		goto out;

	rc = merlin_cfg_build(text, text_size, text_offset, G);
	if (rc < 0) {
		merlin_log((struct merlin_verifier_cfg *)cfg,
			   "REJECT: CFG build failed: %s\n",
			   G->reject_reason ? G->reject_reason : "unknown");
		goto out;
	}

	entry_state = kcalloc(G->nblocks, sizeof(*entry_state), GFP_KERNEL);
	loop_header_ok =
		kcalloc(G->nblocks, sizeof(*loop_header_ok), GFP_KERNEL);
	ws_items = kcalloc(G->nblocks, sizeof(*ws_items), GFP_KERNEL);
	if (!entry_state || !loop_header_ok || !ws_items)
		goto out;
	q.items = ws_items;
	q.cap = (int)G->nblocks;

	vstate_init_entry(&entry_state[0]);
	ws_push(&q, 0);

	while ((i = ws_pop(&q)) >= 0) {
		struct merlin_block *b = &G->blocks[i];
		u32 pending_loop_bound = 0;
		u32 off;
		int s;

		if (!entry_state[i].valid)
			continue;
		memcpy(scratch, &entry_state[i], sizeof(*scratch));

		for (off = b->start_pc - text_offset;
		     off <= b->end_pc - text_offset && off < text_size;
		     off += 4) {
			u32 w = (u32)text[off] | ((u32)text[off + 1] << 8) |
				((u32)text[off + 2] << 16) |
				((u32)text[off + 3] << 24);
			struct merlin_insn in;

			if (merlin_decode(w, text_offset + off, &in) < 0) {
				merlin_log((struct merlin_verifier_cfg *)cfg,
					   "REJECT: pc=%u undecipherable\n",
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
				merlin_log((struct merlin_verifier_cfg *)cfg,
					   "REJECT: step cap %u exceeded\n",
					   step_cap);
				rejected++;
				goto finalize;
			}

			step_insn(in, scratch, cfg, &rejected,
				  &pending_loop_bound);
		}

		if (pending_loop_bound != 0 && b->succ[0] >= 0)
			loop_header_ok[b->succ[0]] = true;

		for (s = 0; s < 2; s++) {
			int n = b->succ[s];
			if (n < 0)
				continue;
			if (vstate_join(&entry_state[n], scratch))
				ws_push(&q, n);
		}
	}

	for (i = 0; i < (int)G->back_edge_count; i++) {
		int src = G->back_edges[i].src;
		int dst = G->back_edges[i].dst;
		if (!loop_header_ok[dst]) {
			merlin_log((struct merlin_verifier_cfg *)cfg,
				   "REJECT: unguarded back edge %d -> %d "
				   "(no merlin_loop_bound)\n",
				   src, dst);
			rejected++;
		}
	}

finalize:
	if (G)
		merlin_log((struct merlin_verifier_cfg *)cfg,
			   "blocks=%u back_edges=%u steps=%u "
			   "insns=%zu helper=%zu jmp=%zu branch=%zu "
			   "load=%zu store=%zu rejected=%u\n",
			   G->nblocks, G->back_edge_count, steps, insn_count,
			   insn_helper, insn_jmp, insn_branch, insn_load,
			   insn_store, rejected);

	result = rejected ? MERLIN_VERIFY_REJECT : MERLIN_VERIFY_OK;
out:
	kfree(entry_state);
	kfree(loop_header_ok);
	kfree(ws_items);
	kfree(scratch);
	kfree(G);
	return result;
}
EXPORT_SYMBOL_GPL(merlin_verify_text);

/* ----- per-prog-type cfg builder ----- */
void merlin_verifier_cfg_for_prog(struct merlin_verifier_cfg *cfg,
				  enum merlin_prog_type prog_type,
				  u32 prog_flags)
{
	memset(cfg, 0, sizeof(*cfg));

	if (prog_flags & MERLIN_F_SLEEPABLE)
		cfg->max_stack_bytes = MERLIN_STACK_SIZE_SLEEPABLE;
	else
		cfg->max_stack_bytes = MERLIN_STACK_SIZE_DEFAULT;

	cfg->allow_back_edges = true; /* gated by merlin_loop_bound */
	cfg->step_cap = MERLIN_VERIFY_STEP_CAP_DEFAULT;

	/* Always permit the loop-bound annotation; programs that don't
 * loop simply never emit it.
 */
	cfg->helper_allow[MERLIN_HELPER_LOOP_BOUND / 8] |=
		(1u << (MERLIN_HELPER_LOOP_BOUND % 8));

	switch (prog_type) {
	case MERLIN_PROG_TYPE_XDP_V:
	case MERLIN_PROG_TYPE_TC_V:
	case MERLIN_PROG_TYPE_MVDP:
		cfg->helper_allow[0] |= (1u << 1); /* helper id 1 = trace */
		break;
	default:
		break;
	}
}
EXPORT_SYMBOL_GPL(merlin_verifier_cfg_for_prog);
