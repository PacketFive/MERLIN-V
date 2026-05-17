// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * helpers.c - host-side stub trampoline for the MERLIN-V JIT prototype.
 *
 * The JIT'd code invokes this trampoline on every ecall.  The
 * trampoline reads a7 (helper id) from regs[17], a0..a5 from
 * regs[10..15], dispatches to the registered helper, and writes
 * the return value back to a0.
 *
 * For the prototype we stub every helper to a sentinel-returning
 * function so we can prove the JIT calls really get to the right
 * place and the return-value path works.  Real helper bodies will
 * land in kernel/merlin/helpers.c when the in-tree work begins.
 */

#include <stdint.h>
#include <stdio.h>

#include "jit.h"

/* RV register indices we care about in the trampoline */
#define R_A0   10
#define R_A1   11
#define R_A2   12
#define R_A3   13
#define R_A4   14
#define R_A5   15
#define R_A7   17

/* Stub helper bodies.  Each returns a recognisable sentinel so test
 * code can confirm the trampoline reached the intended dispatch arm.
 */
static uint64_t stub_map_lookup_elem(uint64_t a0, uint64_t a1)
{
	(void)a0; (void)a1;
	return 0xDEADBEEF;
}

static uint64_t stub_ktime_get_ns(void)
{
	return 0xCAFE0000ull;
}

static uint64_t stub_mvdp_redirect(uint64_t ifindex, uint64_t flags)
{
	(void)ifindex; (void)flags;
	return 0x4EDCAB1Eull;
}

static uint64_t stub_get_prandom_u32(void)
{
	return 0xC001D00Dull;
}

/* Allow tests to override the trampoline behaviour by snooping or
 * mutating call counts.  Bare-bones for the prototype.
 *
 * Helper IDs live in 0x0000 .. 0x0FFF (see
 * docs/design/uapi/merlin/helpers.h); 4096 slots covers the whole
 * legal range.
 */
unsigned long merlin_helper_calls[4096];

void merlin_jit_helper_trampoline(uint64_t *regs)
{
	uint32_t id = (uint32_t)regs[R_A7];
	uint64_t a0 = regs[R_A0], a1 = regs[R_A1];
	uint64_t ret;

	if (id < 4096)
		merlin_helper_calls[id]++;

	switch (id) {
	case 0x0101: ret = stub_map_lookup_elem(a0, a1); break;
	case 0x0110: ret = stub_ktime_get_ns();          break;
	case 0x0120: ret = stub_get_prandom_u32();       break;
	case 0x0201: ret = stub_mvdp_redirect(a0, a1);   break;
	default:
		/* Unknown helper id makes it to the trampoline only if
		 * the verifier failed to gate it.  Report and return
		 * a recognisable error sentinel.
		 */
		fprintf(stderr,
			"[helper-trampoline] WARNING: unknown helper id 0x%x\n",
			id);
		ret = 0xBADC0DE5ull;
	}
	regs[R_A0] = ret;
}
