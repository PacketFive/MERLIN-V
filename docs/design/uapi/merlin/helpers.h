/* SPDX-License-Identifier: GPL-2.0 OR BSD-2-Clause */
/*
 * <merlin/helpers.h> - MERLIN-V helper IDs and invocation intrinsics.
 *
 * Helpers are kernel-provided functions a verified MERLIN-V program
 * may call.  Each helper has a stable numeric ID (the value loaded
 * into a7 at the call site) and a fixed signature checked by the
 * verifier.  See docs/design/02-isa-and-bytecode.md §6 for the ABI
 * and the loader-rewrite mechanic; see docs/design/04-toolchain.md
 * §helpers for the assignment policy.
 *
 * ID allocation (pinned):
 *
 *    0x0000 .. 0x00FF    reserved (invalid; verifier rejects)
 *    0x0100 .. 0x01FF    common helpers (any program type)
 *    0x0200 .. 0x02FF    MVDP / network data path helpers
 *    0x0300 .. 0x03FF    tracing helpers (kprobe/tracepoint/perf)
 *    0x0400 .. 0x04FF    socket-filter helpers
 *    0x0500 .. 0x07FF    reserved for future MERLIN-V program types
 *    0x0800 .. 0x0FFF    reserved for vendor / accelerator helpers
 *    0x1000 ..           illegal (does not fit 12-bit immediate;
 *                        verifier rejects to keep the 8-byte
 *                        "li a7, ID; ecall" loader rewrite valid)
 *
 * IDs are UAPI - they are part of the wire format of a MERLIN-V
 * ELF object.  Adding a helper is an append-only operation that
 * promises forever-stability of its ID.  Removing or repurposing
 * an ID is forbidden.
 */
#ifndef _MERLIN_HELPERS_H
#define _MERLIN_HELPERS_H

#include <merlin/types.h>

/* ----------------------------------------------------------------
 *  Common helpers (0x0100 .. 0x01FF)
 * ---------------------------------------------------------------- */

#define MERLIN_H_MAP_LOOKUP_ELEM          0x0101
#define MERLIN_H_MAP_UPDATE_ELEM          0x0102
#define MERLIN_H_MAP_DELETE_ELEM          0x0103
#define MERLIN_H_MAP_PUSH_ELEM            0x0104  /* ringbuf */
#define MERLIN_H_MAP_POP_ELEM             0x0105
#define MERLIN_H_MAP_PEEK_ELEM            0x0106

#define MERLIN_H_KTIME_GET_NS             0x0110
#define MERLIN_H_KTIME_GET_BOOT_NS        0x0111
#define MERLIN_H_KTIME_GET_TAI_NS         0x0112

#define MERLIN_H_GET_PRANDOM_U32          0x0120
#define MERLIN_H_GET_SMP_PROCESSOR_ID     0x0121
#define MERLIN_H_GET_NUMA_NODE_ID         0x0122

#define MERLIN_H_TRACE_PRINTK             0x0130  /* gated by sysctl */
#define MERLIN_H_TRACE_VPRINTK            0x0131

#define MERLIN_H_PROG_TAIL_CALL           0x0140
#define MERLIN_H_PROG_CURRENT_ID          0x0141

#define MERLIN_H_RINGBUF_RESERVE          0x0150
#define MERLIN_H_RINGBUF_SUBMIT           0x0151
#define MERLIN_H_RINGBUF_DISCARD          0x0152
#define MERLIN_H_RINGBUF_QUERY            0x0153
#define MERLIN_H_RINGBUF_OUTPUT           0x0154

#define MERLIN_H_SPIN_LOCK                0x0160
#define MERLIN_H_SPIN_UNLOCK              0x0161

#define MERLIN_H_PROBE_READ_KERNEL        0x0170
#define MERLIN_H_PROBE_READ_USER          0x0171
#define MERLIN_H_PROBE_READ_KERNEL_STR    0x0172
#define MERLIN_H_PROBE_READ_USER_STR      0x0173

/* ----------------------------------------------------------------
 *  MVDP / network data-path helpers (0x0200 .. 0x02FF)
 * ---------------------------------------------------------------- */

#define MERLIN_H_MVDP_REDIRECT            0x0201
#define MERLIN_H_MVDP_REDIRECT_MAP        0x0202
#define MERLIN_H_MVDP_ADJUST_HEAD         0x0203
#define MERLIN_H_MVDP_ADJUST_TAIL         0x0204
#define MERLIN_H_MVDP_ADJUST_META         0x0205
#define MERLIN_H_MVDP_LOAD_BYTES          0x0206
#define MERLIN_H_MVDP_STORE_BYTES         0x0207
#define MERLIN_H_MVDP_FIB_LOOKUP          0x0208
#define MERLIN_H_MVDP_GET_TIME_NS         0x0209
#define MERLIN_H_MVDP_FRAG_NEXT           0x020A  /* multi-buf */
#define MERLIN_H_MVDP_FRAG_COUNT          0x020B
#define MERLIN_H_MVDP_CSUM_REPLACE2       0x020C
#define MERLIN_H_MVDP_CSUM_REPLACE4       0x020D
#define MERLIN_H_MVDP_L4_CSUM_REPLACE     0x020E

/* ----------------------------------------------------------------
 *  Tracing helpers (0x0300 .. 0x03FF)
 * ---------------------------------------------------------------- */

#define MERLIN_H_GET_CURRENT_PID_TGID     0x0301
#define MERLIN_H_GET_CURRENT_UID_GID      0x0302
#define MERLIN_H_GET_CURRENT_COMM         0x0303
#define MERLIN_H_GET_CURRENT_TASK         0x0304
#define MERLIN_H_GET_STACKID              0x0310
#define MERLIN_H_GET_STACK                0x0311
#define MERLIN_H_PERF_EVENT_OUTPUT        0x0320
#define MERLIN_H_PERF_EVENT_READ          0x0321
#define MERLIN_H_PERF_EVENT_READ_VALUE    0x0322

/* ----------------------------------------------------------------
 *  Socket-filter helpers (0x0400 .. 0x04FF)
 * ---------------------------------------------------------------- */

#define MERLIN_H_SKB_LOAD_BYTES           0x0401
#define MERLIN_H_SKB_STORE_BYTES          0x0402
#define MERLIN_H_SKB_PULL_DATA            0x0403
#define MERLIN_H_SKB_CHANGE_TYPE          0x0404
#define MERLIN_H_SKB_GET_TUNNEL_KEY       0x0410
#define MERLIN_H_SKB_SET_TUNNEL_KEY       0x0411

/* ----------------------------------------------------------------
 *  Invocation intrinsic
 * ----------------------------------------------------------------
 *
 * Every helper call expands to exactly the canonical 8-byte source-
 * level sequence "li a7, ID; ecall".  The loader rewrites this to
 * "auipc t0, %pcrel_hi(trampoline); jalr ra, %pcrel_lo(...)(t0)"
 * at install time (see 02-isa-and-bytecode.md §6.4).  The intrinsic
 * below produces this sequence regardless of compiler optimisation
 * level - the inline assembly is the truth, not the C signature.
 *
 * The C signature gives the verifier the types of the arguments
 * via DWARF / BTF; the verifier checks them against the helper's
 * registered signature at every call site.
 *
 * Usage:
 *
 *    void *value = (void *)merlin_helper_call6(MERLIN_H_MAP_LOOKUP_ELEM,
 *                                              (__u64)&my_map,
 *                                              (__u64)&key,
 *                                              0, 0, 0, 0);
 *
 * In practice programs use the typed wrappers below rather than
 * the raw intrinsic.
 */

#if defined(__riscv) && (__riscv_xlen == 64 || __riscv_xlen == 32)

static __inline__ __attribute__((always_inline))
__u64 merlin_helper_call6(__u64 id,
			  __u64 a0_arg, __u64 a1_arg, __u64 a2_arg,
			  __u64 a3_arg, __u64 a4_arg, __u64 a5_arg)
{
	register __u64 a7 __asm__("a7") = id;
	register __u64 a0 __asm__("a0") = a0_arg;
	register __u64 a1 __asm__("a1") = a1_arg;
	register __u64 a2 __asm__("a2") = a2_arg;
	register __u64 a3 __asm__("a3") = a3_arg;
	register __u64 a4 __asm__("a4") = a4_arg;
	register __u64 a5 __asm__("a5") = a5_arg;

	__asm__ __volatile__ (
		"ecall"
		: "+r"(a0)
		: "r"(a7), "r"(a1), "r"(a2), "r"(a3), "r"(a4), "r"(a5)
		: "memory"
	);
	return a0;
}

#else

/* Off-target compilation (e.g. host tests, doc builds): the intrinsic
 * resolves to an undefined symbol so any program that actually issues
 * a helper call fails to link off-target.  This keeps the headers
 * usable for type-checking on the host without silently miscompiling.
 */
extern __u64 merlin_helper_call6(__u64 id,
				 __u64 a0, __u64 a1, __u64 a2,
				 __u64 a3, __u64 a4, __u64 a5);

#endif /* __riscv */

/* Typed wrappers for the helper set programs actually use.  Each
 * wrapper preserves the helper's C-level signature so the verifier
 * (consulting BTF) can typecheck call sites.  The compiler inlines
 * the wrapper; the resulting object code is the canonical 8-byte
 * "li a7, ID; ecall" sequence.
 */

/* --- common --- */

struct merlin_map;  /* opaque map handle */

static __inline__ __attribute__((always_inline))
void *merlin_map_lookup_elem(const struct merlin_map *map, const void *key)
{
	return (void *)merlin_helper_call6(MERLIN_H_MAP_LOOKUP_ELEM,
					   (__u64)(unsigned long)map,
					   (__u64)(unsigned long)key,
					   0, 0, 0, 0);
}

static __inline__ __attribute__((always_inline))
long merlin_map_update_elem(const struct merlin_map *map,
			    const void *key, const void *value, __u64 flags)
{
	return (long)merlin_helper_call6(MERLIN_H_MAP_UPDATE_ELEM,
					 (__u64)(unsigned long)map,
					 (__u64)(unsigned long)key,
					 (__u64)(unsigned long)value,
					 flags, 0, 0);
}

static __inline__ __attribute__((always_inline))
long merlin_map_delete_elem(const struct merlin_map *map, const void *key)
{
	return (long)merlin_helper_call6(MERLIN_H_MAP_DELETE_ELEM,
					 (__u64)(unsigned long)map,
					 (__u64)(unsigned long)key,
					 0, 0, 0, 0);
}

static __inline__ __attribute__((always_inline))
__u64 merlin_ktime_get_ns(void)
{
	return merlin_helper_call6(MERLIN_H_KTIME_GET_NS,
				   0, 0, 0, 0, 0, 0);
}

static __inline__ __attribute__((always_inline))
__u32 merlin_get_prandom_u32(void)
{
	return (__u32)merlin_helper_call6(MERLIN_H_GET_PRANDOM_U32,
					  0, 0, 0, 0, 0, 0);
}

static __inline__ __attribute__((always_inline))
__u32 merlin_get_smp_processor_id(void)
{
	return (__u32)merlin_helper_call6(MERLIN_H_GET_SMP_PROCESSOR_ID,
					  0, 0, 0, 0, 0, 0);
}

/* --- MVDP-specific wrappers live in <merlin/mvdp.h> --- */

#endif /* _MERLIN_HELPERS_H */
