/* SPDX-License-Identifier: GPL-2.0 OR BSD-2-Clause */
/*
 * <merlin/stats.h> - canonical per-program telemetry counter block.
 *
 * Pinned by docs/design/09-mvcp-kernel-uapi.md §3.5.  The dispatch
 * shim (in-kernel) and the user-space prototype emit exactly this
 * layout; the tracefs binary file, the netlink-multicast samples,
 * and the trailing portion of struct merlin_prog_info all carry
 * the same struct.
 *
 * Layout is fixed UAPI.  Forward-compat: the `size` field MAY be
 * larger than sizeof(struct merlin_prog_stats_v1) on hosts running
 * a newer kernel; readers must consult `size` and ignore tail
 * fields they do not understand.
 */
#ifndef _MERLIN_STATS_H
#define _MERLIN_STATS_H

#include <merlin/types.h>

#define MERLIN_STATS_V1_VERDICTS  8

struct merlin_prog_stats_v1 {
	__u32 size;              /* sizeof(struct) as emitted by this kernel */
	__u32 _pad;
	__u64 run_count;         /* number of times the program ran            */
	__u64 run_ns_total;      /* total ns spent inside the program          */
	__u64 verdict_count[MERLIN_STATS_V1_VERDICTS];
	                         /* indexed by enum mvdp_action /              */
	                         /* equivalent per program type;               */
	                         /* index >= 8 is folded into [0] = ABORTED.   */
	__u64 helper_call_count;
	__u64 helper_fault_count;/* helpers that returned an error             */
	__u64 verifier_load_ns;  /* set once at load; never updated after      */
	__u64 last_run_time_ns;  /* CLOCK_BOOTTIME ns of the last invocation   */
	__u64 _reserved[4];
};

#endif /* _MERLIN_STATS_H */
