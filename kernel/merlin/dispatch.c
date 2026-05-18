// SPDX-License-Identifier: GPL-2.0-only
/*
 * dispatch.c — MERLIN-V helper dispatch.
 *
 * When a MERLIN-V program executes an `ecall`, the JIT-generated code
 * calls merlin_dispatch_helper().  This function looks up the helper id
 * in the global registry and invokes it.
 *
 * Phase 1 ships a small set of built-in helpers that mirror the most
 * common eBPF helpers.  The full helper catalogue is specified in
 * docs/design/02-isa-and-bytecode.md §6.
 *
 * Helper ABI (RISC-V calling convention passed through by the JIT):
 *   id  → a7 (x17)
 *   a0-a5 (x10-x15) → arguments 0-5
 *   return value → a0 (x10)
 */

#include <linux/bpf.h>
#include <linux/filter.h>
#include <linux/module.h>
#include <linux/printk.h>
#include <linux/string.h>
#include <linux/types.h>

#include "include/merlin_internal.h"

/* -----------------------------------------------------------------------
 * Built-in helper implementations
 * ----------------------------------------------------------------------- */

/* Helper 1: merlin_trace_printk(fmt, fmt_size, arg1, arg2, arg3)
 * Mirrors BPF_FUNC_trace_printk; used for smoke-testing.
 */
static u64 helper_trace_printk(u64 a0, u64 a1, u64 a2, u64 a3,
				u64 a4, u64 a5)
{
	/* a0 = fmt ptr (user-context kernel pointer), a1 = fmt_size */
	const char *fmt = (const char *)(uintptr_t)a0;

	/* Safety: the verifier ensures the pointer is ctx-derived or
	 * map-value-derived.  For the prototype we just printk.
	 */
	pr_debug("merlin_trace: %s\n", fmt ? fmt : "(null)");
	(void)a1; (void)a2; (void)a3; (void)a4; (void)a5;
	return 0;
}

/* Helper 2: merlin_map_lookup_elem(map_handle, key_ptr) -> value_ptr | 0
 * Placeholder: real implementation delegates to bpf_map_lookup_elem.
 */
static u64 helper_map_lookup(u64 a0, u64 a1, u64 a2, u64 a3,
			     u64 a4, u64 a5)
{
	/* TODO: resolve map_handle to struct bpf_map *, call
	 * map->ops->map_lookup_elem(map, key_ptr).
	 */
	(void)a0; (void)a1; (void)a2; (void)a3; (void)a4; (void)a5;
	return 0;
}

/* Helper 3: merlin_map_update_elem(map_handle, key_ptr, value_ptr, flags) */
static u64 helper_map_update(u64 a0, u64 a1, u64 a2, u64 a3,
			     u64 a4, u64 a5)
{
	(void)a0; (void)a1; (void)a2; (void)a3; (void)a4; (void)a5;
	return -EOPNOTSUPP;
}

/* Helper 4: merlin_map_delete_elem(map_handle, key_ptr) */
static u64 helper_map_delete(u64 a0, u64 a1, u64 a2, u64 a3,
			     u64 a4, u64 a5)
{
	(void)a0; (void)a1; (void)a2; (void)a3; (void)a4; (void)a5;
	return -EOPNOTSUPP;
}

/* Helper 5: merlin_ktime_get_ns() -> u64 */
static u64 helper_ktime_get_ns(u64 a0, u64 a1, u64 a2, u64 a3,
				u64 a4, u64 a5)
{
	(void)a0; (void)a1; (void)a2; (void)a3; (void)a4; (void)a5;
	return ktime_get_ns();
}

/* Helper 6: merlin_get_prandom_u32() -> u32 */
static u64 helper_prandom_u32(u64 a0, u64 a1, u64 a2, u64 a3,
			      u64 a4, u64 a5)
{
	(void)a0; (void)a1; (void)a2; (void)a3; (void)a4; (void)a5;
	return get_random_u32();
}

/* -----------------------------------------------------------------------
 * Helper registry
 *
 * Indexed by helper id.  Slots 0 and >6 are NULL in phase 1.
 * ----------------------------------------------------------------------- */
#define MERLIN_NR_BUILTIN_HELPERS  7

static const struct merlin_helper_entry builtin_helpers[MERLIN_NR_BUILTIN_HELPERS] = {
	[0] = { .id = 0, .name = "(reserved)", .fn = NULL },
	[1] = { .id = 1, .name = "trace_printk",   .fn = helper_trace_printk },
	[2] = { .id = 2, .name = "map_lookup_elem", .fn = helper_map_lookup   },
	[3] = { .id = 3, .name = "map_update_elem", .fn = helper_map_update   },
	[4] = { .id = 4, .name = "map_delete_elem", .fn = helper_map_delete   },
	[5] = { .id = 5, .name = "ktime_get_ns",    .fn = helper_ktime_get_ns },
	[6] = { .id = 6, .name = "get_prandom_u32", .fn = helper_prandom_u32  },
};

/* -----------------------------------------------------------------------
 * merlin_dispatch_helper — called from JIT-generated ecall sequences
 * ----------------------------------------------------------------------- */
u64 merlin_dispatch_helper(u32 id, u64 a0, u64 a1, u64 a2,
			   u64 a3, u64 a4, u64 a5)
{
	if (id < MERLIN_NR_BUILTIN_HELPERS && builtin_helpers[id].fn)
		return builtin_helpers[id].fn(a0, a1, a2, a3, a4, a5);

	pr_warn_ratelimited("merlin: unknown helper id %u\n", id);
	return (u64)-EINVAL;
}
EXPORT_SYMBOL_GPL(merlin_dispatch_helper);
