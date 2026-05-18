/* SPDX-License-Identifier: Apache-2.0 */
/*
 * dispatch.c — helper registry for the MERLIN-V Zephyr runtime.
 *
 * When a verified MERLIN-V program executes ECALL with a helper id in
 * its allowlist, the runtime calls merlin_dispatch_helper().  The
 * registry is a fixed-size array keyed by helper id; built-in helpers
 * occupy ids 1..6, application-registered helpers may take 7..511.
 */

#include "merlin_internal.h"

#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

/* Maximum helper id this runtime supports (bitset-driven; matches
 * verifier_cfg::max_helper_id default of 511).
 */
#define MERLIN_MAX_HELPER 512

struct helper_entry {
	const char       *name;
	merlin_helper_fn  fn;
};

static struct helper_entry helpers[MERLIN_MAX_HELPER];
static struct k_mutex      helpers_mutex;
static bool                helpers_inited;

static void helpers_init_once(void)
{
	if (helpers_inited)
		return;
	k_mutex_init(&helpers_mutex);
	helpers_inited = true;
}

int merlin_helper_register(uint32_t id, const char *name,
			   merlin_helper_fn fn)
{
	helpers_init_once();

	if (id == 0 || id >= MERLIN_MAX_HELPER || !fn)
		return MERLIN_ERR_INVAL;

	k_mutex_lock(&helpers_mutex, K_FOREVER);
	if (helpers[id].fn) {
		k_mutex_unlock(&helpers_mutex);
		return MERLIN_ERR_BUSY;
	}
	helpers[id].name = name;
	helpers[id].fn   = fn;
	k_mutex_unlock(&helpers_mutex);

	return MERLIN_OK;
}

uint64_t merlin_dispatch_helper(uint32_t id,
				uint64_t a0, uint64_t a1, uint64_t a2,
				uint64_t a3, uint64_t a4, uint64_t a5)
{
	merlin_helper_fn fn = NULL;

	if (id == 0 || id >= MERLIN_MAX_HELPER)
		return (uint64_t)-1;

	if (helpers_inited)
		fn = helpers[id].fn;

	if (!fn) {
		printk("merlin: unknown helper id %u\n", id);
		return (uint64_t)-1;
	}
	return fn(a0, a1, a2, a3, a4, a5);
}
