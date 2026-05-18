/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * ns.h - MVCP namespace prototype: load/save, inheritance, scope-check.
 *
 * In the eventual kernel implementation a namespace is a kernel
 * object referenced by an fd; here we model it as a binary file
 * holding struct merlin_ns_config_v1.  The semantics of
 * inheritance and scope-checking are identical to what the kernel
 * will enforce.
 */
#ifndef MERLIN_NS_H
#define MERLIN_NS_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#include "merlin/namespace.h"

/* Describe the static facts about a program that a namespace
 * scope-check considers.  In the eventual kernel implementation
 * the loader extracts these from the verified MERLIN-V ELF;
 * here we accept them on the command line for the prototype.
 */
struct merlin_prog_facts {
	uint32_t attach_type;            /* enum merlin_attach_type     */
	uint32_t helper_ids[256];        /* program's referenced helpers */
	uint32_t helper_id_count;
	uint32_t kfunc_ids[256];         /* program's referenced kfuncs  */
	uint32_t kfunc_id_count;
	uint64_t map_memory_bytes;       /* sum of map sizes             */
	uint64_t prog_memory_bytes;
};

/* Reasons a scope-check might reject a program. */
enum merlin_ns_reject {
	MERLIN_NS_OK                       = 0,
	MERLIN_NS_REJECT_ATTACH_NOT_ALLOWED = 1,
	MERLIN_NS_REJECT_HELPER_NOT_ALLOWED = 2,
	MERLIN_NS_REJECT_KFUNC_NOT_ALLOWED  = 3,
	MERLIN_NS_REJECT_QUOTA_PROGS        = 4,
	MERLIN_NS_REJECT_QUOTA_MAPS         = 5,
	MERLIN_NS_REJECT_QUOTA_MAP_MEM      = 6,
	MERLIN_NS_REJECT_QUOTA_PROG_MEM     = 7,
	MERLIN_NS_REJECT_SEALED             = 8,
};

/* Describe current quota usage of a namespace (in the prototype
 * the caller maintains this; the kernel will track it). */
struct merlin_ns_usage {
	uint64_t live_progs;
	uint64_t live_maps;
	uint64_t live_map_memory_bytes;
	uint64_t live_prog_memory_bytes;
};

int  merlin_ns_load(const char *path, struct merlin_ns_config_v1 *out);
int  merlin_ns_save(const char *path, const struct merlin_ns_config_v1 *cfg);

/* Compose effective permissions: child intersected with parent.
 * This is what kernel MERLIN_NS_CREATE will compute and seal.
 *
 * Returns 0 on success; -1 if child tries to widen parent in any
 * dimension (kernel rejects with -EPERM; the user-space tool
 * surfaces the offending dimension to stderr).
 */
int merlin_ns_compose(const struct merlin_ns_config_v1 *child,
		      const struct merlin_ns_config_v1 *parent,
		      struct merlin_ns_config_v1 *effective);

/* Decide whether a program with the given facts is permitted to
 * load in the given namespace, with the namespace's current usage.
 * Returns MERLIN_NS_OK or the first failing reason.  An optional
 * out parameter carries the specific offending id (helper / kfunc)
 * for diagnostics.
 */
enum merlin_ns_reject merlin_ns_check(
	const struct merlin_ns_config_v1 *cfg,
	const struct merlin_ns_usage *usage,
	const struct merlin_prog_facts *prog,
	uint32_t *offending_id_out);

const char *merlin_ns_reject_name(enum merlin_ns_reject r);

#endif /* MERLIN_NS_H */
