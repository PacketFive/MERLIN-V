// SPDX-License-Identifier: GPL-2.0-only
/*
 * verifier.c — MERLIN-V abstract-interpretation verifier (kernel port).
 *
 * This is a kernel-C port of tools/merlin-verifier/verify.c.  The
 * algorithm is a linear-pass abstract interpreter over the RISC-V
 * register file.  See docs/design/06-verifier.md for the full strategy.
 *
 * What this version adds over the userland prototype:
 *   - kernel memory (no malloc; stack-allocated state where possible)
 *   - merlin_log() for writing diagnostics into the user-supplied log buffer
 *   - merlin_verifier_cfg_for_prog() to build per-(type,flags) configs
 *   - EXPORT_SYMBOL_GPL on the public API
 *
 * Still prototype-quality (Phase 1):
 *   - Linear pass only; no CFG join points or widening.
 *   - Back-edges detected and rejected (allow_back_edges=false).
 *   - Stack discipline tracked partially (entry sp known; stores not
 *     range-checked yet).
 *   - kfunc resolution deferred to Phase 2.
 *   - C-extension (16-bit) instructions rejected ("UNSUPPORTED" class).
 *
 * Phase 2 work items are marked TODO(phase2).
 */

#include <linux/module.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/types.h>

#include "include/merlin_internal.h"

/* -----------------------------------------------------------------------
 * RISC-V ABI register names (for log diagnostics)
 * ----------------------------------------------------------------------- */
static const char * const xreg_name[32] = {
	"zero","ra","sp","gp","tp","t0","t1","t2",
	"s0/fp","s1","a0","a1","a2","a3","a4","a5",
	"a6","a7","s2","s3","s4","s5","s6","s7",
	"s8","s9","s10","s11","t3","t4","t5","t6",
};

static inline const char *xreg(unsigned r)
{
	return r < 32 ? xreg_name[r] : "?";
}

/* -----------------------------------------------------------------------
 * Abstract register domain helpers
 * ----------------------------------------------------------------------- */
#define RV_UNK()         ((struct merlin_rval){ .kind = RVAL_UNKNOWN })
#define RV_CONST(v)      ((struct merlin_rval){ .kind = RVAL_CONST,       .val = (u64)(v) })
#define RV_PTR_CTX(o)    ((struct merlin_rval){ .kind = RVAL_PTR_CTX,     .val = (u64)(o) })
#define RV_PTR_STACK(o)  ((struct merlin_rval){ .kind = RVAL_PTR_STACK,   .val = (u64)(o) })
#define RV_PTR_HRET(id)  ((struct merlin_rval){ .kind = RVAL_PTR_HELPER_RET, .extra = (id) })

static const char *rval_kind_name(enum merlin_rval_kind k)
{
	switch (k) {
	case RVAL_UNKNOWN:         return "Unknown";
	case RVAL_CONST:           return "Const";
	case RVAL_PTR_CTX:         return "PtrCtx";
	case RVAL_PTR_STACK:       return "PtrStack";
	case RVAL_PTR_HELPER_RET:  return "PtrHelperRet";
	default:                   return "?";
	}
}

static void vstate_init_entry(struct merlin_vstate *s)
{
	int i;

	for (i = 0; i < 32; i++)
		s->x[i] = RV_UNK();
	s->x[0]  = RV_CONST(0);     /* x0 always zero                        */
	s->x[10] = RV_PTR_CTX(0);   /* a0 = ctx at entry (MERLIN convention) */
	s->x[2]  = RV_PTR_STACK(0); /* sp = stack frame base                 */
}

/* -----------------------------------------------------------------------
 * Helper-allowlist bit accessors
 * ----------------------------------------------------------------------- */
static inline bool helper_allowed(const struct merlin_verifier_cfg *cfg, u32 id)
{
	if (id > MERLIN_MAX_HELPER_ID)
		return false;
	return !!(cfg->helper_allow[id / 8] & (1u << (id % 8)));
}

/* -----------------------------------------------------------------------
 * Rejection macro — writes to log buffer, increments rejected counter.
 * The outer function returns MERLIN_VERIFY_REJECT after the loop.
 * ----------------------------------------------------------------------- */
#define REJECT(fmt, ...) do {                                            \
	rejected++;                                                      \
	merlin_log((struct merlin_verifier_cfg *)(cfg),                  \
		   "pc=%u: REJECT: " fmt "\n", in.pc, ##__VA_ARGS__);   \
} while (0)

#define VTRACE(fmt, ...) do {                                            \
	if (cfg->verbose)                                                \
		merlin_log((struct merlin_verifier_cfg *)(cfg),          \
			   "  pc=%u: " fmt "\n", in.pc, ##__VA_ARGS__); \
} while (0)

/* -----------------------------------------------------------------------
 * merlin_verify_text — verify one .text.merlin.* section
 *
 * text/text_size: raw little-endian RV instruction bytes.
 * text_offset: section byte offset within the ELF, used for pc display.
 * cfg: verifier configuration (helper allowlist, max stack, ...).
 *
 * Returns MERLIN_VERIFY_OK or MERLIN_VERIFY_REJECT.
 * Diagnostics are written to cfg->log_buf.
 * ----------------------------------------------------------------------- */
enum merlin_verify_result merlin_verify_text(
	const u8 *text, size_t text_size, u32 text_offset,
	const struct merlin_verifier_cfg *cfg)
{
	struct merlin_vstate s;
	size_t off;
	u32 rejected = 0;
	/* instruction-class counters for the summary */
	size_t insn_count = 0, insn_helper = 0, insn_jmp = 0;
	size_t insn_branch = 0, insn_load = 0, insn_store = 0;

	if (text_size % 4 != 0) {
		merlin_log((struct merlin_verifier_cfg *)cfg,
			   "REJECT: text size %zu is not a multiple of 4\n",
			   text_size);
		return MERLIN_VERIFY_REJECT;
	}

	vstate_init_entry(&s);

	for (off = 0; off < text_size; off += 4) {
		struct merlin_insn in;
		u32 insn_word;
		int rc;

		/* Read little-endian 32-bit word */
		insn_word  = (u32)text[off];
		insn_word |= (u32)text[off + 1] << 8;
		insn_word |= (u32)text[off + 2] << 16;
		insn_word |= (u32)text[off + 3] << 24;

		in.pc = text_offset + (u32)off;
		rc = merlin_decode(insn_word, in.pc, &in);

		if (rc < 0) {
			REJECT("undecipherable bits %08x", insn_word);
			continue;
		}

		insn_count++;
		VTRACE("%s rd=x%u rs1=x%u rs2=x%u imm=%lld",
		       merlin_insn_class_name(in.cls),
		       in.rd, in.rs1, in.rs2, (long long)in.imm);

		/* --- Profile compliance check --- */
		if (!merlin_insn_permitted_default(&in)) {
			REJECT("forbidden instruction class %s (raw=%08x)",
			       merlin_insn_class_name(in.cls), insn_word);
			continue;
		}

		/* --- Back-edge detection --- */
		if ((in.cls == INSN_JAL || in.cls == INSN_BRANCH) && in.imm < 0) {
			if (!cfg->allow_back_edges) {
				REJECT("back-edge at pc=%u (imm=%lld)",
				       in.pc, (long long)in.imm);
			}
			/* TODO(phase2): bounded-loop analysis */
		}

		/* --- ECALL: validate helper ABI --- */
		if (in.cls == INSN_ECALL) {
			insn_helper++;
			struct merlin_rval a7 = s.x[17];

			if (a7.kind != RVAL_CONST) {
				REJECT("ecall with non-constant a7 (kind=%s) "
				       "— helper id must be compile-time known",
				       rval_kind_name(a7.kind));
				/* a0 becomes unknown after a rejected helper */
				s.x[10] = RV_UNK();
				continue;
			}
			if (!helper_allowed(cfg, (u32)a7.val)) {
				REJECT("ecall helper id %llu not in allowlist",
				       (unsigned long long)a7.val);
				s.x[10] = RV_UNK();
				continue;
			}
			/* Valid helper call: a0 becomes PtrHelperRet */
			s.x[10] = RV_PTR_HRET((u32)a7.val);
			/* Caller-saved regs a1..a7, t0..t6 clobbered */
			{
				int r;
				/* a1-a7 */
				for (r = 11; r <= 17; r++)
					s.x[r] = RV_UNK();
				/* t0-t2 (x5-x7), t3-t6 (x28-x31) */
				for (r = 5; r <= 7; r++)
					s.x[r] = RV_UNK();
				for (r = 28; r <= 31; r++)
					s.x[r] = RV_UNK();
			}
			continue;
		}

		/* --- JAL/JALR: jump tracking --- */
		if (in.cls == INSN_JAL || in.cls == INSN_JALR) {
			insn_jmp++;
			/* rd = return address (PtrStack is conservative) */
			if (in.rd != 0)
				s.x[in.rd] = RV_UNK();
			continue;
		}

		/* --- BRANCH --- */
		if (in.cls == INSN_BRANCH) {
			insn_branch++;
			/* TODO(phase2): split state at branch, merge at target */
			continue;
		}

		/* --- LOAD: address must derive from a typed root --- */
		if (in.cls == INSN_LOAD) {
			insn_load++;
			enum merlin_rval_kind k = s.x[in.rs1].kind;

			if (k == RVAL_UNKNOWN || k == RVAL_CONST) {
				REJECT("load through non-pointer reg %s "
				       "(kind=%s, imm=%lld)",
				       xreg(in.rs1),
				       rval_kind_name(k),
				       (long long)in.imm);
			}
			/* rd gets an unknown scalar after the load */
			if (in.rd != 0)
				s.x[in.rd] = RV_UNK();
			continue;
		}

		/* --- STORE: address must derive from a typed root --- */
		if (in.cls == INSN_STORE) {
			insn_store++;
			enum merlin_rval_kind k = s.x[in.rs1].kind;

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
			struct merlin_rval src = s.x[in.rs1];

			if (in.rs1 == 0) {
				/* addi rd, x0, imm  →  rd = Const(imm) */
				s.x[in.rd] = RV_CONST((u64)in.imm);
			} else if (src.kind == RVAL_CONST) {
				s.x[in.rd] = RV_CONST(src.val + (u64)in.imm);
			} else if (src.kind == RVAL_PTR_CTX ||
				   src.kind == RVAL_PTR_STACK) {
				/* pointer offset arithmetic: preserve root */
				s.x[in.rd] = (struct merlin_rval){
					.kind  = src.kind,
					.val   = src.val + (u64)in.imm,
					.extra = src.extra,
				};
			} else {
				s.x[in.rd] = RV_UNK();
			}
			continue;
		}

		if (in.cls == INSN_LUI) {
			if (in.rd != 0)
				s.x[in.rd] = RV_CONST((u64)in.imm);
			continue;
		}

		if (in.cls == INSN_AUIPC) {
			/* PC-relative; conservatively Unknown */
			if (in.rd != 0)
				s.x[in.rd] = RV_UNK();
			continue;
		}

		/* Default: any other instruction clobbers rd to Unknown */
		if (in.rd != 0)
			s.x[in.rd] = RV_UNK();
	}

	merlin_log((struct merlin_verifier_cfg *)cfg,
		   "insns=%zu helper-calls=%zu jmp=%zu branch=%zu "
		   "load=%zu store=%zu rejected=%u\n",
		   insn_count, insn_helper, insn_jmp, insn_branch,
		   insn_load, insn_store, rejected);

	return rejected ? MERLIN_VERIFY_REJECT : MERLIN_VERIFY_OK;
}
EXPORT_SYMBOL_GPL(merlin_verify_text);

/* -----------------------------------------------------------------------
 * merlin_verifier_cfg_for_prog — build a verifier config for a given
 * (prog_type, prog_flags) pair.
 *
 * The helper allowlist and stack-size limit depend on the program type.
 * Phase 1: only the MVDP and XDP_V types are wired up; other types get
 * a conservative all-zeros allowlist (no helpers permitted).
 * ----------------------------------------------------------------------- */
void merlin_verifier_cfg_for_prog(struct merlin_verifier_cfg *cfg,
				  enum merlin_prog_type prog_type,
				  u32 prog_flags)
{
	memset(cfg, 0, sizeof(*cfg));

	if (prog_flags & MERLIN_F_SLEEPABLE)
		cfg->max_stack_bytes = MERLIN_STACK_SIZE_SLEEPABLE;
	else
		cfg->max_stack_bytes = MERLIN_STACK_SIZE_DEFAULT;

	cfg->allow_back_edges = false;  /* Phase 2: bounded-loop helper */

	/* TODO(phase2): populate helper_allow per-type from a registry. */
	switch (prog_type) {
	case MERLIN_PROG_TYPE_XDP_V:
	case MERLIN_PROG_TYPE_TC_V:
	case MERLIN_PROG_TYPE_MVDP:
		/* Allow helper 1 (trace_printk equivalent) for smoke testing. */
		cfg->helper_allow[0] |= (1u << 1);
		break;
	default:
		break;
	}
}
EXPORT_SYMBOL_GPL(merlin_verifier_cfg_for_prog);
