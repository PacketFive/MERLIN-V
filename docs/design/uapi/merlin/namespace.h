/* SPDX-License-Identifier: GPL-2.0 OR BSD-2-Clause */
/*
 * <merlin/namespace.h> - canonical MVCP namespace config layout.
 *
 * Pinned by docs/design/09-mvcp-kernel-uapi.md §3.4.  A MERLIN
 * namespace scopes:
 *
 *   - which MERLIN_ATTACH_* attach types its programs may use,
 *   - which helper IDs its programs may call,
 *   - which kfunc type-IDs its programs may reference,
 *   - which trust-root keyring backs signed-program enforcement,
 *   - how much program / map / memory quota the namespace gets.
 *
 * A namespace MAY have a parent.  Inheritance rule:
 *
 *   A child namespace can only ever be a SUBSET of its parent in
 *   every dimension (you can narrow, never widen).  This is the
 *   property the kernel will enforce on MERLIN_NS_CREATE; the
 *   user-space prototype enforces the same rule in its config
 *   loader so the executable-spec semantics match what the kernel
 *   will do.
 */
#ifndef _MERLIN_NAMESPACE_H
#define _MERLIN_NAMESPACE_H

#include <merlin/types.h>

#define MERLIN_NS_MAGIC      0x4D4E5356u   /* 'VSNM' little-endian */
#define MERLIN_NS_VERSION    1u

#define MERLIN_NS_NAME_MAX   32

/*
 * Permitted-attach bitset.  Bit n set => MERLIN_ATTACH_<type>=n is
 * allowed for programs loaded in this namespace.  Indexed by
 * enum merlin_attach_type (uapi/linux/merlin.h).
 */
#define MERLIN_NS_ATTACH_WORDS    2          /* 64 bits = enough for v1 */

/*
 * Permitted-helper bitset, indexed by helper ID (0x0000..0x0FFF).
 * 4096 bits = 64 __u64 words.  Same allocation policy as
 * <merlin/helpers.h> (IDs >= 0x1000 are illegal).
 */
#define MERLIN_NS_HELPER_WORDS    64

/*
 * Permitted-kfunc-id bitset.  The bit index is the BTF type-id of
 * the kfunc in the program-side BTF, capped at 4096 for v1.  Future
 * revisions may grow this; size_v1 records the v1 word count.
 */
#define MERLIN_NS_KFUNC_WORDS     64

struct merlin_ns_config_v1 {
	__u32  magic;                /* MERLIN_NS_MAGIC                  */
	__u32  size;                 /* sizeof(struct) emitted           */
	__u32  version;              /* MERLIN_NS_VERSION                */
	__u32  flags;                /* MERLIN_NS_F_*                    */

	char   name[MERLIN_NS_NAME_MAX];  /* NUL-padded                  */
	__u32  parent_ns_id;         /* 0 if root                        */
	__u32  attest_policy;        /* enum merlin_ns_attest_policy
					(see 11-mvcp-attestation.md §7)  */

	/* Permission bitsets */
	__u64  permit_attach[MERLIN_NS_ATTACH_WORDS];
	__u64  permit_helper[MERLIN_NS_HELPER_WORDS];
	__u64  permit_kfunc [MERLIN_NS_KFUNC_WORDS];

	/* Quotas */
	__u64  max_progs;            /* 0 = unlimited                    */
	__u64  max_maps;
	__u64  max_map_memory_bytes;
	__u64  max_prog_memory_bytes;

	/* Trust-root keyring binding (opaque kernel key_serial_t in the
	 * eventual kernel implementation; in the user-space prototype
	 * a path to a directory of accepted public keys). */
	__u32  trust_keyring_id;
	__u32  _reserved[7];         /* MBZ                              */
};

/*
 * Namespace flags.
 *
 *  MERLIN_NS_F_INHERIT      child inherits parent permission sets
 *                           (union-with-parent at load; the merge
 *                           is monotonically narrowing per the
 *                           subset rule).
 *  MERLIN_NS_F_SEALED       no further modifications accepted;
 *                           kernel rejects MERLIN_NS_UPDATE.
 *  MERLIN_NS_F_VISIBLE_TO_CHILDREN
 *                           parent's prog list is read-visible to
 *                           children via MERLIN_PROG_GET_NEXT_ID.
 */
#define MERLIN_NS_F_INHERIT               (1u << 0)
#define MERLIN_NS_F_SEALED                (1u << 1)
#define MERLIN_NS_F_VISIBLE_TO_CHILDREN   (1u << 2)

/* Bitset helpers - inlined so consumers don't need a separate .c.   */
static inline int merlin_ns_bit_get(const __u64 *bs, unsigned idx,
				    unsigned nwords)
{
	if (idx / 64u >= nwords) return 0;
	return (int)((bs[idx / 64u] >> (idx & 63u)) & 1u);
}
static inline void merlin_ns_bit_set(__u64 *bs, unsigned idx,
				     unsigned nwords)
{
	if (idx / 64u >= nwords) return;
	bs[idx / 64u] |= ((__u64)1u << (idx & 63u));
}
static inline void merlin_ns_bit_clear(__u64 *bs, unsigned idx,
				       unsigned nwords)
{
	if (idx / 64u >= nwords) return;
	bs[idx / 64u] &= ~((__u64)1u << (idx & 63u));
}

#endif /* _MERLIN_NAMESPACE_H */
