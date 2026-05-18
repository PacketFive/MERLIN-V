/* SPDX-License-Identifier: Apache-2.0 */
/*
 * verifier.c — MERLIN-V abstract-interpretation verifier (Zephyr build).
 *
 * Cut-down kernel port: linear-pass abstract interpreter over the
 * rtos-rv32 profile.  Same rejection criteria as kernel/merlin/verifier.c:
 *   - instruction outside the rtos-rv32 profile
 *   - back-edges (negative-immediate JAL/BRANCH)
 *   - ecall whose a7 is not a compile-time constant in the allowlist
 *   - LOAD/STORE through a non-pointer register
 *
 * Caller is expected to point cfg->log_buf at a small buffer (~512 B)
 * to receive the verifier's one-line summary.
 */

#include "merlin_internal.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#define RV_UNK()        ((struct merlin_rval){ .kind = RVAL_UNKNOWN })
#define RV_CONST(v)     ((struct merlin_rval){ .kind = RVAL_CONST,     .val = (uint32_t)(v) })
#define RV_PTR_CTX(o)   ((struct merlin_rval){ .kind = RVAL_PTR_CTX,   .val = (uint32_t)(o) })
#define RV_PTR_STACK(o) ((struct merlin_rval){ .kind = RVAL_PTR_STACK, .val = (uint32_t)(o) })

void merlin_log(struct merlin_verifier_cfg *cfg, const char *fmt, ...)
{
	va_list ap;

	if (!cfg->log_buf || cfg->log_pos >= cfg->log_buf_sz)
		return;
	va_start(ap, fmt);
	cfg->log_pos += vsnprintf(cfg->log_buf + cfg->log_pos,
				  cfg->log_buf_sz - cfg->log_pos, fmt, ap);
	va_end(ap);
}

static void vstate_init_entry(struct merlin_vstate *s)
{
	for (int i = 0; i < 32; i++)
		s->x[i] = RV_UNK();
	s->x[0]  = RV_CONST(0);
	s->x[10] = RV_PTR_CTX(0);
	s->x[2]  = RV_PTR_STACK(0);
}

static bool helper_allowed(const struct merlin_verifier_cfg *cfg, uint32_t id)
{
	if (id > cfg->max_helper_id)
		return false;
	if (id / 8 >= sizeof(cfg->helper_allow))
		return false;
	return !!(cfg->helper_allow[id / 8] & (1u << (id % 8)));
}

int merlin_verify_text(const uint8_t *text, size_t text_size,
		       struct merlin_verifier_cfg *cfg)
{
	struct merlin_vstate s;

	cfg->insns_seen = 0;
	cfg->rejected   = 0;

	if (text_size == 0 || text_size % 4) {
		merlin_log(cfg, "REJECT: text size %u not aligned\n",
			   (unsigned)text_size);
		return -1;
	}

	vstate_init_entry(&s);

	for (size_t off = 0; off < text_size; off += 4) {
		struct merlin_insn in;
		uint32_t w = (uint32_t)text[off]
			   | ((uint32_t)text[off + 1] << 8)
			   | ((uint32_t)text[off + 2] << 16)
			   | ((uint32_t)text[off + 3] << 24);

		if (merlin_decode(w, (uint32_t)off, &in) < 0) {
			merlin_log(cfg, "pc=%u: REJECT: undecipherable %08x\n",
				   (unsigned)off, w);
			cfg->rejected++;
			continue;
		}
		cfg->insns_seen++;

		if (!merlin_insn_permitted_rtos(&in)) {
			merlin_log(cfg, "pc=%u: REJECT: forbidden class %s\n",
				   in.pc, merlin_insn_class_name(in.cls));
			cfg->rejected++;
			continue;
		}

		if ((in.cls == INSN_JAL || in.cls == INSN_BRANCH) &&
		    in.imm < 0 && !cfg->allow_back_edges) {
			merlin_log(cfg, "pc=%u: REJECT: back-edge imm=%d\n",
				   in.pc, in.imm);
			cfg->rejected++;
		}

		if (in.cls == INSN_ECALL) {
			struct merlin_rval a7 = s.x[17];

			if (a7.kind != RVAL_CONST) {
				merlin_log(cfg, "pc=%u: REJECT: ecall non-const a7\n",
					   in.pc);
				cfg->rejected++;
			} else if (!helper_allowed(cfg, a7.val)) {
				merlin_log(cfg, "pc=%u: REJECT: helper id %u not allowed\n",
					   in.pc, (unsigned)a7.val);
				cfg->rejected++;
			} else {
				s.x[10] = (struct merlin_rval){
					.kind = RVAL_PTR_HELPER_RET,
					.val  = a7.val
				};
			}
			/* clobber caller-saved */
			for (int r = 11; r <= 17; r++) s.x[r] = RV_UNK();
			for (int r = 5;  r <= 7;  r++) s.x[r] = RV_UNK();
			for (int r = 28; r <= 31; r++) s.x[r] = RV_UNK();
			continue;
		}

		if (in.cls == INSN_LOAD || in.cls == INSN_STORE) {
			enum merlin_rval_kind k = s.x[in.rs1].kind;

			if (k == RVAL_UNKNOWN || k == RVAL_CONST) {
				merlin_log(cfg, "pc=%u: REJECT: %s thru x%u kind=%d\n",
					   in.pc,
					   in.cls == INSN_LOAD ? "load" : "store",
					   in.rs1, (int)k);
				cfg->rejected++;
			}
			if (in.cls == INSN_LOAD && in.rd != 0)
				s.x[in.rd] = RV_UNK();
			continue;
		}

		/* ALU effects */
		if (in.cls == INSN_ALU_IMM && in.alu_op == ALU_ADD) {
			struct merlin_rval src = s.x[in.rs1];

			if (in.rs1 == 0) {
				s.x[in.rd] = RV_CONST(in.imm);
			} else if (src.kind == RVAL_CONST) {
				s.x[in.rd] = RV_CONST(src.val + (uint32_t)in.imm);
			} else if (src.kind == RVAL_PTR_CTX ||
				   src.kind == RVAL_PTR_STACK) {
				s.x[in.rd] = (struct merlin_rval){
					.kind = src.kind,
					.val  = src.val + (uint32_t)in.imm,
				};
			} else if (in.rd != 0) {
				s.x[in.rd] = RV_UNK();
			}
			continue;
		}

		if (in.cls == INSN_LUI) {
			if (in.rd != 0) s.x[in.rd] = RV_CONST(in.imm);
			continue;
		}
		if (in.cls == INSN_AUIPC) {
			if (in.rd != 0) s.x[in.rd] = RV_UNK();
			continue;
		}

		if (in.rd != 0)
			s.x[in.rd] = RV_UNK();
	}

	merlin_log(cfg, "insns=%u rejected=%u\n",
		   cfg->insns_seen, cfg->rejected);

	return cfg->rejected ? -1 : 0;
}

void merlin_verifier_cfg_for_prog(struct merlin_verifier_cfg *cfg,
				  enum merlin_rtos_prog_type pt)
{
	memset(cfg, 0, sizeof(*cfg));
	cfg->max_helper_id    = (sizeof(cfg->helper_allow) * 8) - 1;
	cfg->max_stack_bytes  = CONFIG_MERLIN_MAX_STACK_BYTES;
	cfg->allow_back_edges = false;

	/* Allow helpers 1..6 (basic set) for every prog type for now. */
	cfg->helper_allow[0] |= (1u << 1) | (1u << 2) | (1u << 3) |
				(1u << 4) | (1u << 5) | (1u << 6);

	(void)pt;
}
