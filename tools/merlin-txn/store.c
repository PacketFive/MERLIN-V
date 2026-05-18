// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * store.c - in-memory map store and transaction engine.
 */
#include "store.h"

#include <errno.h>
#include <string.h>
#include <stdio.h>
#include <time.h>

/* ------------------------------------------------------------------ */
/* Global map table                                                     */
/* ------------------------------------------------------------------ */

static struct merlin_map g_maps[MERLIN_STORE_MAX_MAPS];

void merlin_store_init(void)
{
	memset(g_maps, 0, sizeof(g_maps));
	for (uint32_t i = 0; i < MERLIN_STORE_MAX_MAPS; i++)
		g_maps[i].handle = i + 1;  /* handles are 1-based */
}

uint32_t merlin_store_map_create(void)
{
	for (uint32_t i = 0; i < MERLIN_STORE_MAX_MAPS; i++) {
		if (!g_maps[i].valid) {
			g_maps[i].valid   = true;
			g_maps[i].count   = 0;
			g_maps[i].version = 0;
			memset(g_maps[i].entries, 0,
			       sizeof(g_maps[i].entries));
			return g_maps[i].handle;
		}
	}
	return 0;
}

void merlin_store_map_destroy(uint32_t handle)
{
	if (!handle || handle > MERLIN_STORE_MAX_MAPS) return;
	g_maps[handle - 1].valid = false;
	g_maps[handle - 1].count = 0;
}

static struct merlin_map *find_map(uint32_t handle)
{
	if (!handle || handle > MERLIN_STORE_MAX_MAPS) return NULL;
	struct merlin_map *m = &g_maps[handle - 1];
	return m->valid ? m : NULL;
}

/* ------------------------------------------------------------------ */
/* Read operations                                                      */
/* ------------------------------------------------------------------ */

bool merlin_store_lookup(uint32_t handle, uint64_t key, uint64_t *val_out)
{
	struct merlin_map *m = find_map(handle);
	if (!m) return false;
	for (uint32_t i = 0; i < MERLIN_STORE_MAX_ENTRIES; i++) {
		if (m->entries[i].used && m->entries[i].key == key) {
			if (val_out) *val_out = m->entries[i].value;
			return true;
		}
	}
	return false;
}

uint32_t merlin_store_count(uint32_t handle)
{
	struct merlin_map *m = find_map(handle);
	return m ? m->count : 0;
}

bool merlin_store_next(uint32_t handle, size_t *pos,
		       uint64_t *key_out, uint64_t *val_out)
{
	struct merlin_map *m = find_map(handle);
	if (!m) return false;
	while (*pos < MERLIN_STORE_MAX_ENTRIES) {
		size_t i = (*pos)++;
		if (m->entries[i].used) {
			if (key_out) *key_out = m->entries[i].key;
			if (val_out) *val_out = m->entries[i].value;
			return true;
		}
	}
	return false;
}

/* ------------------------------------------------------------------ */
/* Low-level map mutations (used by commit engine only)                */
/* ------------------------------------------------------------------ */

static int map_insert(struct merlin_map *m, uint64_t key, uint64_t val)
{
	/* fail if key exists */
	for (uint32_t i = 0; i < MERLIN_STORE_MAX_ENTRIES; i++)
		if (m->entries[i].used && m->entries[i].key == key)
			return -EEXIST;
	for (uint32_t i = 0; i < MERLIN_STORE_MAX_ENTRIES; i++) {
		if (!m->entries[i].used) {
			m->entries[i].used  = true;
			m->entries[i].key   = key;
			m->entries[i].value = val;
			m->count++;
			return 0;
		}
	}
	return -ENOSPC;
}

static int map_update(struct merlin_map *m, uint64_t key, uint64_t val)
{
	for (uint32_t i = 0; i < MERLIN_STORE_MAX_ENTRIES; i++) {
		if (m->entries[i].used && m->entries[i].key == key) {
			m->entries[i].value = val;
			return 0;
		}
	}
	return -ENOENT;
}

static int map_upsert(struct merlin_map *m, uint64_t key, uint64_t val)
{
	for (uint32_t i = 0; i < MERLIN_STORE_MAX_ENTRIES; i++) {
		if (m->entries[i].used && m->entries[i].key == key) {
			m->entries[i].value = val;
			return 0;
		}
	}
	/* not found -> insert */
	return map_insert(m, key, val);
}

static int map_delete(struct merlin_map *m, uint64_t key)
{
	for (uint32_t i = 0; i < MERLIN_STORE_MAX_ENTRIES; i++) {
		if (m->entries[i].used && m->entries[i].key == key) {
			m->entries[i].used = false;
			m->count--;
			return 0;
		}
	}
	return -ENOENT;  /* caller should treat as no-op for DELETE */
}

static void map_replace_all(struct merlin_map *m,
			    const uint64_t *keys,
			    const uint64_t *vals,
			    uint32_t n)
{
	memset(m->entries, 0, sizeof(m->entries));
	m->count = 0;
	for (uint32_t i = 0; i < n && i < MERLIN_STORE_MAX_ENTRIES; i++) {
		m->entries[i].used  = true;
		m->entries[i].key   = keys[i];
		m->entries[i].value = vals[i];
		m->count++;
	}
}

/* ------------------------------------------------------------------ */
/* Per-namespace transaction serialisation                              */
/*                                                                      */
/* In the kernel: a per-namespace spinlock serialises commits on maps   */
/* whose handle-sets overlap.  Here we maintain a "currently-committing */
/* map set" bitmask per ns_id; a new commit stalls if the sets overlap. */
/* The prototype is single-threaded so we detect the conflict by         */
/* checking a per-map "locked" flag set/cleared around each commit.      */
/* ------------------------------------------------------------------ */

static bool g_map_locked[MERLIN_STORE_MAX_MAPS];  /* locked by a commit */

static uint64_t now_ns(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (uint64_t)ts.tv_sec * 1000000000ull + ts.tv_nsec;
}

/* ------------------------------------------------------------------ */
/* Transaction API                                                      */
/* ------------------------------------------------------------------ */

int merlin_txn_begin(struct merlin_txn *txn, uint32_t ns_id)
{
	memset(txn, 0, sizeof(*txn));
	txn->open   = true;
	txn->ns_id  = ns_id;
	return 0;
}

int merlin_txn_stage(struct merlin_txn *txn,
		     uint32_t map_handle,
		     enum merlin_txn_op_type op,
		     uint64_t key, uint64_t value)
{
	if (!txn->open || txn->committed || txn->aborted)
		return -EINVAL;
	if (txn->op_count >= MERLIN_TXN_MAX_OPS)
		return -ENOSPC;
	if (!find_map(map_handle))
		return -EBADF;

	txn->ops[txn->op_count++] = (struct merlin_txn_op_v1){
		.map_handle = map_handle,
		.op         = op,
		.key        = key,
		.value      = value,
	};

	/* Track touched set for disjoint-commit check. */
	bool found = false;
	for (uint32_t i = 0; i < txn->touched_count; i++)
		if (txn->touched[i] == map_handle) { found = true; break; }
	if (!found && txn->touched_count < MERLIN_STORE_MAX_MAPS)
		txn->touched[txn->touched_count++] = map_handle;

	return 0;
}

int merlin_txn_commit(struct merlin_txn *txn, uint32_t flags,
		      struct merlin_txn_stats_v1 *stats_out)
{
	if (!txn->open || txn->committed || txn->aborted)
		return -EINVAL;

	uint64_t t0 = now_ns();

	/* Conflict detection: check if any touched map is already locked
	 * by another in-flight commit (simulates the per-ns txn lock for
	 * overlapping map sets). */
	if (!(flags & MERLIN_TXN_F_FAST)) {
		for (uint32_t i = 0; i < txn->touched_count; i++) {
			uint32_t h = txn->touched[i];
			if (h && h <= MERLIN_STORE_MAX_MAPS
			    && g_map_locked[h - 1]) {
				if (stats_out) {
					stats_out->conflict_map_handle = h;
				}
				return -EBUSY;  /* -ECONFLICT in kernel */
			}
		}
	}

	/* Lock the touched maps for the duration of this commit. */
	for (uint32_t i = 0; i < txn->touched_count; i++) {
		uint32_t h = txn->touched[i];
		if (h && h <= MERLIN_STORE_MAX_MAPS)
			g_map_locked[h - 1] = true;
	}

	/* Apply all staged ops.  On any hard error (-ENOSPC), roll back
	 * by re-snapshotting is complex; the prototype returns -ENOSPC
	 * and aborts.  All other "soft" errors (EEXIST on INSERT,
	 * ENOENT on DELETE) are counted as skipped but not fatal. */
	uint32_t applied = 0, skipped = 0;
	int rc = 0;

	for (uint32_t i = 0; i < txn->op_count; i++) {
		struct merlin_txn_op_v1 *op = &txn->ops[i];
		struct merlin_map *m = find_map(op->map_handle);
		if (!m) { rc = -EBADF; break; }

		int r = 0;
		switch (op->op) {
		case MERLIN_MAP_TXN_INSERT:
			r = map_insert(m, op->key, op->value);
			break;
		case MERLIN_MAP_TXN_UPDATE:
			r = map_update(m, op->key, op->value);
			break;
		case MERLIN_MAP_TXN_UPSERT:
			r = map_upsert(m, op->key, op->value);
			break;
		case MERLIN_MAP_TXN_DELETE:
			r = map_delete(m, op->key);
			/* -ENOENT on DELETE is a soft skip, not an error. */
			if (r == -ENOENT) { skipped++; continue; }
			break;
		case MERLIN_MAP_TXN_REPLACE_ALL:
			/* value carries a count; key is a base-index hack
			 * (prototype only). Real API uses a separate array. */
			map_replace_all(m, &op->key, &op->value, 1);
			r = 0;
			break;
		default:
			r = -EINVAL;
			break;
		}

		if (r == -EEXIST || r == -ENOENT) {
			skipped++;
		} else if (r < 0) {
			rc = r;
			break;
		} else {
			applied++;
			m->version++;
		}
	}

	/* RCU drain simulation: a real DRAIN_RCU waits for all CPUs to
	 * quiesce.  Here we just record that it was requested. */
	bool drained = !(flags & MERLIN_TXN_F_FAST);

	/* Unlock touched maps. */
	for (uint32_t i = 0; i < txn->touched_count; i++) {
		uint32_t h = txn->touched[i];
		if (h && h <= MERLIN_STORE_MAX_MAPS)
			g_map_locked[h - 1] = false;
	}

	uint64_t elapsed = now_ns() - t0;

	if (stats_out) {
		stats_out->ops_staged   = txn->op_count;
		stats_out->ops_applied  = applied;
		stats_out->ops_skipped  = skipped;
		stats_out->conflict_map_handle = 0;
		stats_out->commit_time_ns = elapsed;
	}

	if (rc == 0) {
		txn->committed = true;
		txn->open      = false;
	} else {
		txn->aborted = true;
		txn->open    = false;
	}

	(void)drained;
	return rc;
}

void merlin_txn_abort(struct merlin_txn *txn)
{
	txn->open     = false;
	txn->aborted  = true;
}
