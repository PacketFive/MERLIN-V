// SPDX-License-Identifier: GPL-2.0-only
/*
 * maps.c — MERLIN-V map operations.
 *
 * MERLIN-V reuses kernel/bpf/ map infrastructure.  MERLIN_MAP_*
 * commands delegate to the corresponding bpf_map_* paths, translating
 * between the merlin_map_type enum and BPF_MAP_TYPE_* integers.
 *
 * Cross-reference: docs/design/03-kernel-interfaces.md §5
 */

#include <linux/bpf.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/uaccess.h>

#include "include/merlin_internal.h"

/* -----------------------------------------------------------------------
 * merlin_map_type → BPF_MAP_TYPE_* translation
 *
 * Numbers are chosen to be 1:1 where semantics are identical; new types
 * (e.g. MVSKMAP) have no BPF equivalent and return BPF_MAP_TYPE_UNSPEC
 * as a sentinel until they are implemented natively.
 * ----------------------------------------------------------------------- */
static enum bpf_map_type merlin_to_bpf_map_type(enum merlin_map_type mt)
{
	switch (mt) {
	case MERLIN_MAP_TYPE_HASH:         return BPF_MAP_TYPE_HASH;
	case MERLIN_MAP_TYPE_ARRAY:        return BPF_MAP_TYPE_ARRAY;
	case MERLIN_MAP_TYPE_PERCPU_HASH:  return BPF_MAP_TYPE_PERCPU_HASH;
	case MERLIN_MAP_TYPE_PERCPU_ARRAY: return BPF_MAP_TYPE_PERCPU_ARRAY;
	case MERLIN_MAP_TYPE_LRU_HASH:     return BPF_MAP_TYPE_LRU_HASH;
	case MERLIN_MAP_TYPE_LPM_TRIE:     return BPF_MAP_TYPE_LPM_TRIE;
	case MERLIN_MAP_TYPE_RINGBUF:      return BPF_MAP_TYPE_RINGBUF;
	case MERLIN_MAP_TYPE_PROG_ARRAY:   return BPF_MAP_TYPE_PROG_ARRAY;
	case MERLIN_MAP_TYPE_XSKMAP:       return BPF_MAP_TYPE_XSKMAP;
	case MERLIN_MAP_TYPE_MVSKMAP:      return BPF_MAP_TYPE_UNSPEC; /* TBD */
	default:                           return BPF_MAP_TYPE_UNSPEC;
	}
}

/* -----------------------------------------------------------------------
 * merlin_map_create — MERLIN_MAP_CREATE handler
 *
 * Translates the merlin_attr to a bpf_attr and delegates to
 * map_create() via the bpf_map_get_curr_or_next / bpf syscall path.
 *
 * For the prototype we call bpf_map_area_alloc indirectly by constructing
 * a struct bpf_attr and calling into the kernel BPF map create path via
 * the exported symbol (available from bpf-next).  This approach keeps
 * the merlin map subsystem thin and avoids duplicating storage code.
 *
 * TODO: wire up a direct struct bpf_map * allocation once this is in-tree
 * and we can call map_create() directly.
 * ----------------------------------------------------------------------- */
int merlin_map_create(const union merlin_attr __user *uattr, u32 attr_sz)
{
	union merlin_attr attr;
	struct bpf_attr battr;
	enum bpf_map_type bmt;

	memset(&attr, 0, sizeof(attr));
	if (copy_from_user(&attr, uattr, min_t(u32, attr_sz, sizeof(attr))))
		return -EFAULT;

	bmt = merlin_to_bpf_map_type((enum merlin_map_type)attr.map_create.map_type);
	if (bmt == BPF_MAP_TYPE_UNSPEC)
		return -EOPNOTSUPP;

	memset(&battr, 0, sizeof(battr));
	battr.map_type    = bmt;
	battr.key_size    = attr.map_create.key_size;
	battr.value_size  = attr.map_create.value_size;
	battr.max_entries = attr.map_create.max_entries;
	battr.map_flags   = attr.map_create.map_flags;
	memcpy(battr.map_name, attr.map_create.map_name,
	       min_t(size_t, sizeof(battr.map_name),
		     sizeof(attr.map_create.map_name)));

	/* Delegate to the BPF syscall path.  In an in-tree build this
	 * would be a direct call; out-of-tree we use __sys_bpf.
	 */
	return __sys_bpf(BPF_MAP_CREATE, KERNEL_BPFPTR(&battr), sizeof(battr));
}
EXPORT_SYMBOL_GPL(merlin_map_create);
