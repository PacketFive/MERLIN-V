/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * store.h - simple in-memory map store for the batch-txn prototype.
 *
 * Models a fixed-width uint64_t->uint64_t hash map.  In the eventual
 * kernel implementation MERLIN maps use RCU-protected per-CPU snapshot
 * pointers; this store uses a plain mutex for simplicity while keeping
 * the API and semantics identical to what the kernel will enforce.
 *
 * Multiple "map handles" live in a global store table; each handle is
 * an integer ID, just as MERLIN map file descriptors work in the kernel.
 */
#ifndef MERLIN_TXN_STORE_H
#define MERLIN_TXN_STORE_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#include "merlin/txn.h"

/* Maximum number of maps in the store. */
#define MERLIN_STORE_MAX_MAPS     64
/* Maximum entries per map. */
#define MERLIN_STORE_MAX_ENTRIES  256

struct merlin_map_entry {
	uint64_t key;
	uint64_t value;
	bool     used;
};

struct merlin_map {
	uint32_t              handle;
	bool                  valid;
	struct merlin_map_entry entries[MERLIN_STORE_MAX_ENTRIES];
	uint32_t              count;       /* live entries           */
	uint32_t              version;     /* bumped on each commit  */
};

/* --- Store lifecycle --- */
void merlin_store_init(void);

/* Create a map; returns its handle (>0) or 0 on error. */
uint32_t merlin_store_map_create(void);

/* Destroy a map (frees its slot). */
void merlin_store_map_destroy(uint32_t handle);

/* --- Single-map read operations (outside any transaction) --- */
bool merlin_store_lookup(uint32_t handle, uint64_t key, uint64_t *val_out);
uint32_t merlin_store_count(uint32_t handle);
/* Iterate: call with *pos=0; returns true while there are more entries;
 * advances *pos on each call. */
bool merlin_store_next(uint32_t handle, size_t *pos,
		       uint64_t *key_out, uint64_t *val_out);

/* --- Transaction object --- */
struct merlin_txn {
	bool     open;
	bool     committed;
	bool     aborted;
	uint32_t ns_id;          /* for the per-ns lock model  */

	struct merlin_txn_op_v1 ops[MERLIN_TXN_MAX_OPS];
	uint32_t                op_count;

	/* which map handles are touched (for disjoint-set detection) */
	uint32_t  touched[MERLIN_STORE_MAX_MAPS];
	uint32_t  touched_count;
};

/* Begin a transaction.  Returns 0 on success. */
int merlin_txn_begin(struct merlin_txn *txn, uint32_t ns_id);

/* Stage an op.  Returns 0 on success; -ENOSPC if MERLIN_TXN_MAX_OPS. */
int merlin_txn_stage(struct merlin_txn *txn,
		     uint32_t map_handle,
		     enum merlin_txn_op_type op,
		     uint64_t key, uint64_t value);

/*
 * Commit the transaction.
 *
 * flags: MERLIN_TXN_F_DRAIN_RCU (default) or MERLIN_TXN_F_FAST.
 * DRAIN_RCU is a no-op in user space (no kernel RCU), but is
 * recorded in the stats to show the path is taken.
 *
 * Returns 0 on success; -ECONFLICT if an overlapping transaction
 * committed concurrently (simulated by a per-ns version check).
 */
int merlin_txn_commit(struct merlin_txn *txn, uint32_t flags,
		      struct merlin_txn_stats_v1 *stats_out);

/* Abort the transaction (discard staging log). */
void merlin_txn_abort(struct merlin_txn *txn);

#endif /* MERLIN_TXN_STORE_H */
