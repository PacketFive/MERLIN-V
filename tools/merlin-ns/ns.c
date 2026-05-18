// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * ns.c - namespace load/save/compose/check.
 */
#include "ns.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>

int merlin_ns_load(const char *path, struct merlin_ns_config_v1 *out)
{
	int fd = open(path, O_RDONLY);
	if (fd < 0) return -1;

	struct merlin_ns_config_v1 tmp = {0};
	ssize_t n = read(fd, &tmp, sizeof(tmp));
	close(fd);
	if (n < (ssize_t)sizeof(uint32_t) * 3) return -1;

	if (tmp.magic   != MERLIN_NS_MAGIC)    return -1;
	if (tmp.version != MERLIN_NS_VERSION)  return -1;
	if (tmp.size    != sizeof(tmp))        return -1;
	if (n           < (ssize_t)tmp.size)   return -1;

	*out = tmp;
	return 0;
}

int merlin_ns_save(const char *path, const struct merlin_ns_config_v1 *cfg)
{
	int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
	if (fd < 0) return -1;
	ssize_t n = write(fd, cfg, sizeof(*cfg));
	close(fd);
	return (n == (ssize_t)sizeof(*cfg)) ? 0 : -1;
}

/* Helpers to check "child is a subset of parent" per bitset. */
static int bitset_subset(const uint64_t *child, const uint64_t *parent,
			 unsigned nwords, unsigned *offending_bit)
{
	for (unsigned i = 0; i < nwords; i++) {
		uint64_t illegal = child[i] & ~parent[i];
		if (illegal) {
			/* lowest set bit of `illegal` is the first
			 * dimension where the child widened. */
			unsigned bit = 0;
			while (!((illegal >> bit) & 1u)) bit++;
			*offending_bit = i * 64u + bit;
			return -1;
		}
	}
	return 0;
}

int merlin_ns_compose(const struct merlin_ns_config_v1 *child,
		      const struct merlin_ns_config_v1 *parent,
		      struct merlin_ns_config_v1 *effective)
{
	unsigned bit;
	if (bitset_subset(child->permit_attach, parent->permit_attach,
			  MERLIN_NS_ATTACH_WORDS, &bit) < 0) {
		fprintf(stderr,
			"merlin-ns: child widens attach permission "
			"at bit %u (not allowed in parent)\n", bit);
		return -1;
	}
	if (bitset_subset(child->permit_helper, parent->permit_helper,
			  MERLIN_NS_HELPER_WORDS, &bit) < 0) {
		fprintf(stderr,
			"merlin-ns: child widens helper permission "
			"at helper id 0x%x\n", bit);
		return -1;
	}
	if (bitset_subset(child->permit_kfunc, parent->permit_kfunc,
			  MERLIN_NS_KFUNC_WORDS, &bit) < 0) {
		fprintf(stderr,
			"merlin-ns: child widens kfunc permission "
			"at kfunc btf-id %u\n", bit);
		return -1;
	}

	/* Quotas: child must be <= parent (treating 0 as "unlimited",
	 * which only the root may set).  If parent is finite and
	 * child has 0, child inherits the parent's finite value
	 * (you cannot widen by omission). */
	#define Q(name) do { \
		if (parent->name != 0) { \
			if (child->name == 0) { \
				effective_quota_##name = parent->name; \
			} else if (child->name > parent->name) { \
				fprintf(stderr, \
					"merlin-ns: child widens quota %s " \
					"(parent=%lu child=%lu)\n", \
					#name, \
					(unsigned long)parent->name, \
					(unsigned long)child->name); \
				return -1; \
			} else { \
				effective_quota_##name = child->name; \
			} \
		} else { \
			effective_quota_##name = child->name; \
		} \
	} while (0)
	uint64_t effective_quota_max_progs = 0;
	uint64_t effective_quota_max_maps = 0;
	uint64_t effective_quota_max_map_memory_bytes = 0;
	uint64_t effective_quota_max_prog_memory_bytes = 0;
	Q(max_progs);
	Q(max_maps);
	Q(max_map_memory_bytes);
	Q(max_prog_memory_bytes);
	#undef Q

	/* Effective = child (already subset-checked) with merged quotas. */
	*effective = *child;
	effective->parent_ns_id = parent->parent_ns_id;  /* root grandparent */
	effective->max_progs              = effective_quota_max_progs;
	effective->max_maps               = effective_quota_max_maps;
	effective->max_map_memory_bytes   = effective_quota_max_map_memory_bytes;
	effective->max_prog_memory_bytes  = effective_quota_max_prog_memory_bytes;
	return 0;
}

enum merlin_ns_reject merlin_ns_check(
	const struct merlin_ns_config_v1 *cfg,
	const struct merlin_ns_usage *usage,
	const struct merlin_prog_facts *prog,
	uint32_t *offending_id_out)
{
	if (cfg->flags & MERLIN_NS_F_SEALED) {
		/* Sealed namespaces accept loads matching their permissions;
		 * the seal blocks UPDATE, not LOAD.  Continue to the rest. */
	}

	/* attach */
	if (!merlin_ns_bit_get(cfg->permit_attach,
			       prog->attach_type,
			       MERLIN_NS_ATTACH_WORDS)) {
		if (offending_id_out) *offending_id_out = prog->attach_type;
		return MERLIN_NS_REJECT_ATTACH_NOT_ALLOWED;
	}

	/* helpers */
	for (uint32_t i = 0; i < prog->helper_id_count; i++) {
		uint32_t id = prog->helper_ids[i];
		if (!merlin_ns_bit_get(cfg->permit_helper,
				       id, MERLIN_NS_HELPER_WORDS)) {
			if (offending_id_out) *offending_id_out = id;
			return MERLIN_NS_REJECT_HELPER_NOT_ALLOWED;
		}
	}

	/* kfuncs */
	for (uint32_t i = 0; i < prog->kfunc_id_count; i++) {
		uint32_t id = prog->kfunc_ids[i];
		if (!merlin_ns_bit_get(cfg->permit_kfunc,
				       id, MERLIN_NS_KFUNC_WORDS)) {
			if (offending_id_out) *offending_id_out = id;
			return MERLIN_NS_REJECT_KFUNC_NOT_ALLOWED;
		}
	}

	/* quotas */
	if (cfg->max_progs &&
	    usage->live_progs + 1 > cfg->max_progs)
		return MERLIN_NS_REJECT_QUOTA_PROGS;
	if (cfg->max_maps && usage->live_maps > cfg->max_maps)
		return MERLIN_NS_REJECT_QUOTA_MAPS;
	if (cfg->max_map_memory_bytes &&
	    usage->live_map_memory_bytes + prog->map_memory_bytes
		> cfg->max_map_memory_bytes)
		return MERLIN_NS_REJECT_QUOTA_MAP_MEM;
	if (cfg->max_prog_memory_bytes &&
	    usage->live_prog_memory_bytes + prog->prog_memory_bytes
		> cfg->max_prog_memory_bytes)
		return MERLIN_NS_REJECT_QUOTA_PROG_MEM;

	return MERLIN_NS_OK;
}

const char *merlin_ns_reject_name(enum merlin_ns_reject r)
{
	switch (r) {
	case MERLIN_NS_OK:                          return "OK";
	case MERLIN_NS_REJECT_ATTACH_NOT_ALLOWED:   return "attach_not_allowed";
	case MERLIN_NS_REJECT_HELPER_NOT_ALLOWED:   return "helper_not_allowed";
	case MERLIN_NS_REJECT_KFUNC_NOT_ALLOWED:    return "kfunc_not_allowed";
	case MERLIN_NS_REJECT_QUOTA_PROGS:          return "quota_progs";
	case MERLIN_NS_REJECT_QUOTA_MAPS:           return "quota_maps";
	case MERLIN_NS_REJECT_QUOTA_MAP_MEM:        return "quota_map_mem";
	case MERLIN_NS_REJECT_QUOTA_PROG_MEM:       return "quota_prog_mem";
	case MERLIN_NS_REJECT_SEALED:               return "sealed";
	}
	return "?";
}
