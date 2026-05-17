/* SPDX-License-Identifier: GPL-2.0 OR BSD-2-Clause */
/*
 * <merlin/mvdp.h> - MVDP program declarations.
 *
 * MVDP (MERLIN-V Data Path) is the native MERLIN-V network data
 * path; see docs/design/08-mvdp-and-af-mvdp.md.  This header
 * provides:
 *
 *   - struct mvdp_md (program ctx; verifier-validated layout)
 *   - enum mvdp_action (program return values)
 *   - typed wrappers for MVDP helpers
 *   - MERLIN_PROG_MVDP() program-section macro (re-exported from
 *     <merlin/section_macros.h> for convenience)
 *
 * The struct layout matches <linux/if_mvdp.h>; programs include
 * this header rather than the kernel UAPI header so they remain
 * libc-free.
 */
#ifndef _MERLIN_MVDP_H
#define _MERLIN_MVDP_H

#include <merlin/types.h>
#include <merlin/helpers.h>
#include <merlin/section_macros.h>

/* --- Context layout (mirror of struct mvdp_md in <linux/if_mvdp.h>) --- */
struct mvdp_md {
	__u64 data;
	__u64 data_end;
	__u64 data_meta;
	__u32 ingress_ifindex;
	__u32 rx_queue_index;
	__u32 egress_ifindex;
	__u32 frame_flags;
	__u64 hw_timestamp;
	__u32 csum_status;
	__u32 vlan_tci;
	__u32 hash;
	__u32 _reserved;
};

#ifndef MVDP_FRAME_F_VLAN_PRESENT
#define MVDP_FRAME_F_VLAN_PRESENT       (1U << 0)
#define MVDP_FRAME_F_HASH_VALID         (1U << 1)
#define MVDP_FRAME_F_TIMESTAMP_VALID    (1U << 2)
#endif

#ifndef MVDP_CSUM_NONE
#define MVDP_CSUM_NONE                  0
#define MVDP_CSUM_UNNECESSARY           1
#define MVDP_CSUM_COMPLETE              2
#define MVDP_CSUM_BAD                   3
#endif

/* --- Verdicts --- */
enum mvdp_action {
	MVDP_ABORTED  = 0,
	MVDP_DROP     = 1,
	MVDP_PASS     = 2,
	MVDP_TX       = 3,
	MVDP_REDIRECT = 4,
	MVDP_DELIVER  = 5,
};

/* --- Helper wrappers --- */

static __inline__ __attribute__((always_inline))
long mvdp_redirect(__u32 ifindex, __u64 flags)
{
	return (long)merlin_helper_call6(MERLIN_H_MVDP_REDIRECT,
					 (__u64)ifindex, flags,
					 0, 0, 0, 0);
}

static __inline__ __attribute__((always_inline))
long mvdp_redirect_map(const struct merlin_map *map, __u64 key, __u64 flags)
{
	return (long)merlin_helper_call6(MERLIN_H_MVDP_REDIRECT_MAP,
					 (__u64)(unsigned long)map,
					 key, flags, 0, 0, 0);
}

static __inline__ __attribute__((always_inline))
long mvdp_adjust_head(struct mvdp_md *ctx, __s32 delta)
{
	return (long)merlin_helper_call6(MERLIN_H_MVDP_ADJUST_HEAD,
					 (__u64)(unsigned long)ctx,
					 (__u64)(__s64)delta,
					 0, 0, 0, 0);
}

static __inline__ __attribute__((always_inline))
long mvdp_adjust_tail(struct mvdp_md *ctx, __s32 delta)
{
	return (long)merlin_helper_call6(MERLIN_H_MVDP_ADJUST_TAIL,
					 (__u64)(unsigned long)ctx,
					 (__u64)(__s64)delta,
					 0, 0, 0, 0);
}

static __inline__ __attribute__((always_inline))
long mvdp_adjust_meta(struct mvdp_md *ctx, __s32 delta)
{
	return (long)merlin_helper_call6(MERLIN_H_MVDP_ADJUST_META,
					 (__u64)(unsigned long)ctx,
					 (__u64)(__s64)delta,
					 0, 0, 0, 0);
}

static __inline__ __attribute__((always_inline))
long mvdp_load_bytes(const struct mvdp_md *ctx, __u32 offset,
		     void *dst, __u32 len)
{
	return (long)merlin_helper_call6(MERLIN_H_MVDP_LOAD_BYTES,
					 (__u64)(unsigned long)ctx,
					 (__u64)offset,
					 (__u64)(unsigned long)dst,
					 (__u64)len, 0, 0);
}

static __inline__ __attribute__((always_inline))
long mvdp_store_bytes(struct mvdp_md *ctx, __u32 offset,
		      const void *src, __u32 len, __u64 flags)
{
	return (long)merlin_helper_call6(MERLIN_H_MVDP_STORE_BYTES,
					 (__u64)(unsigned long)ctx,
					 (__u64)offset,
					 (__u64)(unsigned long)src,
					 (__u64)len, flags, 0);
}

static __inline__ __attribute__((always_inline))
__u64 mvdp_get_time_ns(void)
{
	return merlin_helper_call6(MERLIN_H_MVDP_GET_TIME_NS,
				   0, 0, 0, 0, 0, 0);
}

#endif /* _MERLIN_MVDP_H */
