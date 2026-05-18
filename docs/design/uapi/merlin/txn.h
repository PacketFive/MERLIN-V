/* SPDX-License-Identifier: GPL-2.0 OR BSD-2-Clause */
/*
 * <merlin/txn.h> - canonical MVCP map-batch transaction types.
 *
 * Pinned by docs/design/09-mvcp-kernel-uapi.md §3.3.
 *
 * A transaction sequences a set of map operations (INSERT, UPDATE,
 * DELETE, REPLACE_ALL) across one or more MERLIN-V maps.  Commit is
 * atomic-or-none across all staged ops and across all touched maps.
 *
 * Concurrency model:
 *   - Concurrent commits on OVERLAPPING map sets are serialised on
 *     a per-namespace transaction lock.
 *   - Concurrent commits on DISJOINT map sets do not contend.
 *   - MERLIN_TXN_F_DRAIN_RCU (default): waits for an RCU grace
 *     period after commit; caller is guaranteed no in-flight program
 *     observes the pre-commit state on return.
 *   - MERLIN_TXN_F_FAST: skips the drain; caller owns quiesce.
 *
 * Limits:
 *   - Max staged ops per transaction: MERLIN_TXN_MAX_OPS (4096).
 *   - Kernel enforces; sysctl-tunable in the in-kernel implementation.
 */
#ifndef _MERLIN_TXN_H
#define _MERLIN_TXN_H

#include <merlin/types.h>

/* Transaction flags (MERLIN_MAP_BATCH_TXN_COMMIT flags field). */
#define MERLIN_TXN_F_DRAIN_RCU  (1u << 0)   /* wait for RCU grace period  */
#define MERLIN_TXN_F_FAST       (1u << 1)   /* skip drain; caller quiesces */

/* Maximum staged ops per transaction (kernel-enforced). */
#define MERLIN_TXN_MAX_OPS      4096u

/* Staged-operation types. */
enum merlin_txn_op_type {
	MERLIN_MAP_TXN_INSERT      = 1,  /* insert; fail if key exists   */
	MERLIN_MAP_TXN_UPDATE      = 2,  /* update; fail if key absent   */
	MERLIN_MAP_TXN_UPSERT      = 3,  /* insert or update             */
	MERLIN_MAP_TXN_DELETE      = 4,  /* delete; ok if key absent     */
	MERLIN_MAP_TXN_REPLACE_ALL = 5,  /* replace entire map contents  */
};

/*
 * A single staged operation.  In the kernel implementation this is
 * appended to a per-txn staging log via MERLIN_MAP_BATCH_TXN_STAGE;
 * here it is the record the user-space prototype stores.
 *
 * key/value are represented as 64-bit scalars in the prototype for
 * simplicity; the kernel API uses (ptr, len) pairs.
 */
struct merlin_txn_op_v1 {
	__u32 map_handle;           /* opaque map identifier            */
	__u32 op;                   /* enum merlin_txn_op_type          */
	__u64 key;
	__u64 value;                /* unused for DELETE / REPLACE_ALL  */
	__u32 flags;                /* reserved, MBZ                    */
	__u32 _pad;
};

/*
 * Commit-result statistics returned by the commit call.
 * These match the trailing info fields of MERLIN_MAP_BATCH_TXN_COMMIT.
 */
struct merlin_txn_stats_v1 {
	__u32 ops_staged;           /* ops in the transaction           */
	__u32 ops_applied;          /* ops that landed                  */
	__u32 ops_skipped;          /* ops skipped (e.g. DELETE noop)   */
	__u32 conflict_map_handle;  /* 0 or conflicting map handle      */
	__u64 commit_time_ns;       /* elapsed ns for the commit        */
};

#endif /* _MERLIN_TXN_H */
