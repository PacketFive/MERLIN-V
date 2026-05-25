/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * tnum.h - tristate-number tracking primitive for the MERLIN-V verifier.
 *
 * A tnum (Vishwanathan et al., "Sound, Precise, and Fast Abstract
 * Interpretation with Tristate Numbers", CGO 2022) represents an
 * abstract scalar value as two 64-bit words:
 *
 *   value  : known bit-values
 *   mask   : bits we do NOT know (1 = unknown)
 *
 * A bit-position p is "known" iff (mask >> p) & 1 == 0, and in that
 * case its value is (value >> p) & 1.  By construction
 *   value & mask == 0
 * so (value, mask) is canonical.
 *
 * Constants:                 tnum_const(K)  = { value=K,  mask=0 }
 * Top (entirely unknown):    tnum_top()     = { value=0,  mask=~0 }
 * Bottom (uninitialised):    represented via enum rval_kind, not a tnum.
 *
 * This is the same abstract domain the eBPF verifier uses; sharing the
 * domain (per docs/design/06-verifier.md §4) makes the eventual
 * factoring into a shared kernel module mechanical.
 *
 * The header is intentionally header-only and dependency-free so it
 * can be #included from both the userland prototype and the in-kernel
 * port without a separate translation unit.
 */
#ifndef MERLIN_VERIFIER_TNUM_H
#define MERLIN_VERIFIER_TNUM_H

#ifdef __KERNEL__
#include <linux/types.h>
#define MTNUM_U64 u64
#define MTNUM_S64 s64
#else
#include <stdbool.h>
#include <stdint.h>
#define MTNUM_U64 uint64_t
#define MTNUM_S64 int64_t
#endif

struct merlin_tnum {
	MTNUM_U64 value;
	MTNUM_U64 mask;
};

static inline struct merlin_tnum tnum_const(MTNUM_U64 v)
{
	return (struct merlin_tnum){ .value = v, .mask = 0 };
}

static inline struct merlin_tnum tnum_top(void)
{
	return (struct merlin_tnum){ .value = 0, .mask = ~(MTNUM_U64)0 };
}

static inline bool tnum_is_const(struct merlin_tnum t)
{
	return t.mask == 0;
}

static inline bool tnum_is_top(struct merlin_tnum t)
{
	return t.mask == ~(MTNUM_U64)0;
}

static inline bool tnum_equal(struct merlin_tnum a, struct merlin_tnum b)
{
	return a.value == b.value && a.mask == b.mask;
}

/* Pointwise join: for each bit, "either-of" the two abstractions. */
static inline struct merlin_tnum tnum_join(struct merlin_tnum a,
					   struct merlin_tnum b)
{
	MTNUM_U64 v = a.value & b.value;
	MTNUM_U64 mu = a.value ^ b.value; /* bits that disagree    */
	MTNUM_U64 mask = a.mask | b.mask | mu; /* unknown if any side */
	return (struct merlin_tnum){ .value = v & ~mask, .mask = mask };
}

/* Transfer functions for the small set of ops we model precisely. */
static inline struct merlin_tnum tnum_add(struct merlin_tnum a,
					  struct merlin_tnum b)
{
	MTNUM_U64 sm = a.mask + b.mask;
	MTNUM_U64 sv = a.value + b.value;
	MTNUM_U64 sigma = sv + sm;
	MTNUM_U64 chi = sigma ^ sv;
	MTNUM_U64 mu = chi | a.mask | b.mask;
	return (struct merlin_tnum){ .value = sv & ~mu, .mask = mu };
}

static inline struct merlin_tnum tnum_sub(struct merlin_tnum a,
					  struct merlin_tnum b)
{
	MTNUM_U64 dv = a.value - b.value;
	MTNUM_U64 alpha = dv + a.mask;
	MTNUM_U64 beta = dv - b.mask;
	MTNUM_U64 chi = alpha ^ beta;
	MTNUM_U64 mu = chi | a.mask | b.mask;
	return (struct merlin_tnum){ .value = dv & ~mu, .mask = mu };
}

static inline struct merlin_tnum tnum_and(struct merlin_tnum a,
					  struct merlin_tnum b)
{
	MTNUM_U64 alpha = a.value | a.mask; /* bits possibly 1 in a */
	MTNUM_U64 beta = b.value | b.mask;
	MTNUM_U64 v = a.value & b.value;
	return (struct merlin_tnum){ .value = v, .mask = alpha & beta & ~v };
}

static inline struct merlin_tnum tnum_or(struct merlin_tnum a,
					 struct merlin_tnum b)
{
	MTNUM_U64 v = a.value | b.value;
	MTNUM_U64 mu = a.mask | b.mask;
	return (struct merlin_tnum){ .value = v, .mask = mu & ~v };
}

static inline struct merlin_tnum tnum_xor(struct merlin_tnum a,
					  struct merlin_tnum b)
{
	MTNUM_U64 v = a.value ^ b.value;
	MTNUM_U64 mu = a.mask | b.mask;
	return (struct merlin_tnum){ .value = v & ~mu, .mask = mu };
}

static inline struct merlin_tnum tnum_lshift(struct merlin_tnum a, unsigned k)
{
	if (k >= 64)
		return tnum_top();
	return (struct merlin_tnum){
		.value = a.value << k,
		.mask = a.mask << k,
	};
}

static inline struct merlin_tnum tnum_rshift(struct merlin_tnum a, unsigned k)
{
	if (k >= 64)
		return tnum_const(0);
	return (struct merlin_tnum){
		.value = a.value >> k,
		.mask = a.mask >> k,
	};
}

/* ----------------------------------------------------------------------
 * merlin_scalar: tnum + signed/unsigned range.
 *
 * Carrying both forms lets the verifier reason about expressions that
 * cross the signed/unsigned boundary (which RISC-V does on every
 * branch).  Joins widen both ranges; transfer functions tighten when
 * possible.  Bottom == "register not yet defined" is encoded via the
 * outer rval kind (RVAL_UNINIT), not via the scalar.
 * ---------------------------------------------------------------------- */
struct merlin_scalar {
	MTNUM_S64 smin, smax; /* signed range, inclusive          */
	MTNUM_U64 umin, umax; /* unsigned range, inclusive        */
	struct merlin_tnum tn;
};

static inline struct merlin_scalar scalar_const(MTNUM_S64 v)
{
	return (struct merlin_scalar){
		.smin = v,
		.smax = v,
		.umin = (MTNUM_U64)v,
		.umax = (MTNUM_U64)v,
		.tn = tnum_const((MTNUM_U64)v),
	};
}

static inline struct merlin_scalar scalar_top(void)
{
	return (struct merlin_scalar){
		.smin = (MTNUM_S64)(((MTNUM_U64)1) << 63), /* INT64_MIN */
		.smax = (MTNUM_S64)((((MTNUM_U64)1) << 63) - 1), /* INT64_MAX */
		.umin = 0,
		.umax = ~(MTNUM_U64)0,
		.tn = tnum_top(),
	};
}

static inline bool scalar_is_const(struct merlin_scalar s)
{
	return tnum_is_const(s.tn) && s.smin == s.smax;
}

static inline struct merlin_scalar scalar_join(struct merlin_scalar a,
					       struct merlin_scalar b)
{
	struct merlin_scalar j;
	j.smin = a.smin < b.smin ? a.smin : b.smin;
	j.smax = a.smax > b.smax ? a.smax : b.smax;
	j.umin = a.umin < b.umin ? a.umin : b.umin;
	j.umax = a.umax > b.umax ? a.umax : b.umax;
	j.tn = tnum_join(a.tn, b.tn);
	return j;
}

static inline struct merlin_scalar scalar_add(struct merlin_scalar a,
					      struct merlin_scalar b)
{
	struct merlin_scalar r;
	r.tn = tnum_add(a.tn, b.tn);

	/* Saturating signed/unsigned arithmetic on the bounds.  If we
 * detect overflow (the sum doesn't fit), fall back to top for
 * the affected range.  This is the same pattern eBPF uses.
 */
	{
		MTNUM_S64 lo = a.smin + b.smin;
		MTNUM_S64 hi = a.smax + b.smax;
		bool overflow = ((a.smin < 0) == (b.smin < 0) &&
				 (lo < 0) != (a.smin < 0)) ||
				((a.smax < 0) == (b.smax < 0) &&
				 (hi < 0) != (a.smax < 0));
		if (overflow) {
			r.smin = (MTNUM_S64)(((MTNUM_U64)1) << 63);
			r.smax = (MTNUM_S64)((((MTNUM_U64)1) << 63) - 1);
		} else {
			r.smin = lo;
			r.smax = hi;
		}
	}
	{
		MTNUM_U64 lo = a.umin + b.umin;
		MTNUM_U64 hi = a.umax + b.umax;
		bool overflow = (lo < a.umin) || (hi < a.umax);
		if (overflow) {
			r.umin = 0;
			r.umax = ~(MTNUM_U64)0;
		} else {
			r.umin = lo;
			r.umax = hi;
		}
	}
	return r;
}

static inline bool scalar_equal(struct merlin_scalar a, struct merlin_scalar b)
{
	return a.smin == b.smin && a.smax == b.smax && a.umin == b.umin &&
	       a.umax == b.umax && tnum_equal(a.tn, b.tn);
}

#endif /* MERLIN_VERIFIER_TNUM_H */
