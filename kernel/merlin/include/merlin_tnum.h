/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * merlin_tnum.h - tristate-number primitive (kernel-side).
 *
 * Mirrors tools/merlin-verifier/tnum.h exactly; the two share the
 * abstract domain so that the eventual factoring into a shared
 * kernel module (per docs/design/06-verifier.md §4 Option A) is a
 * syntactic change rather than a semantic one.
 *
 * The header is intentionally inline-only and only depends on
 * linux/types.h.
 */
#ifndef _MERLIN_TNUM_H
#define _MERLIN_TNUM_H

#include <linux/types.h>

struct merlin_tnum {
	u64 value;
	u64 mask;
};

static inline struct merlin_tnum tnum_const(u64 v)
{
	return (struct merlin_tnum){ .value = v, .mask = 0 };
}
static inline struct merlin_tnum tnum_top(void)
{
	return (struct merlin_tnum){ .value = 0, .mask = ~(u64)0 };
}
static inline bool tnum_is_const(struct merlin_tnum t)
{
	return t.mask == 0;
}

static inline bool tnum_equal(struct merlin_tnum a, struct merlin_tnum b)
{
	return a.value == b.value && a.mask == b.mask;
}

static inline struct merlin_tnum tnum_join(struct merlin_tnum a,
					   struct merlin_tnum b)
{
	u64 mu = a.mask | b.mask | (a.value ^ b.value);
	return (struct merlin_tnum){ .value = (a.value & b.value) & ~mu,
				     .mask = mu };
}

static inline struct merlin_tnum tnum_add(struct merlin_tnum a,
					  struct merlin_tnum b)
{
	u64 sm = a.mask + b.mask;
	u64 sv = a.value + b.value;
	u64 sigma = sv + sm;
	u64 chi = sigma ^ sv;
	u64 mu = chi | a.mask | b.mask;
	return (struct merlin_tnum){ .value = sv & ~mu, .mask = mu };
}

struct merlin_scalar {
	s64 smin, smax;
	u64 umin, umax;
	struct merlin_tnum tn;
};

static inline struct merlin_scalar scalar_const(s64 v)
{
	return (struct merlin_scalar){
		.smin = v,
		.smax = v,
		.umin = (u64)v,
		.umax = (u64)v,
		.tn = tnum_const((u64)v),
	};
}
static inline struct merlin_scalar scalar_top(void)
{
	return (struct merlin_scalar){
		.smin = S64_MIN,
		.smax = S64_MAX,
		.umin = 0,
		.umax = U64_MAX,
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
	{
		s64 lo = a.smin + b.smin, hi = a.smax + b.smax;
		bool ov = ((a.smin < 0) == (b.smin < 0) &&
			   (lo < 0) != (a.smin < 0)) ||
			  ((a.smax < 0) == (b.smax < 0) &&
			   (hi < 0) != (a.smax < 0));
		if (ov) {
			r.smin = S64_MIN;
			r.smax = S64_MAX;
		} else {
			r.smin = lo;
			r.smax = hi;
		}
	}
	{
		u64 lo = a.umin + b.umin, hi = a.umax + b.umax;
		bool ov = (lo < a.umin) || (hi < a.umax);
		if (ov) {
			r.umin = 0;
			r.umax = U64_MAX;
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

#endif /* _MERLIN_TNUM_H */
