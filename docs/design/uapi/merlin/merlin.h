/* SPDX-License-Identifier: GPL-2.0 OR BSD-2-Clause */
/*
 * <merlin/merlin.h> - umbrella header for MERLIN-V programs.
 *
 * Including this header pulls in everything a typical MERLIN-V
 * program needs:
 *
 *    <merlin/types.h>           fixed-width integer types
 *    <merlin/section_macros.h>  SEC(), MERLIN_PROG_*()
 *    <merlin/license.h>         MERLIN_LICENSE()
 *    <merlin/helpers.h>         helper IDs + invocation intrinsic
 *    <merlin/maps.h>            MERLIN_MAP_DEF()
 *    <merlin/core.h>            CO-RE-V intrinsics
 *
 * Program-type-specific headers (e.g. <merlin/mvdp.h>) are NOT
 * pulled in by default; include them separately.
 *
 * A minimal program looks like:
 *
 *     #include <merlin/merlin.h>
 *     #include <merlin/mvdp.h>
 *
 *     MERLIN_LICENSE("GPL");
 *     MERLIN_META(MERLIN_PROFILE_LINUX_RV64);
 *
 *     MERLIN_PROG_MVDP(drop_all)(struct mvdp_md *ctx)
 *     {
 *         return MVDP_DROP;
 *     }
 */
#ifndef _MERLIN_MERLIN_H
#define _MERLIN_MERLIN_H

#include <merlin/types.h>
#include <merlin/section_macros.h>
#include <merlin/license.h>
#include <merlin/helpers.h>
#include <merlin/maps.h>
#include <merlin/core.h>

/*
 * Profile constants - mirror enum merlin_profile in <linux/merlin.h>.
 * Programs use these to declare their bytecode profile via MERLIN_META.
 */
#ifndef MERLIN_PROFILE_UNSPEC
#define MERLIN_PROFILE_UNSPEC      0
#define MERLIN_PROFILE_LINUX_RV64  1
#define MERLIN_PROFILE_RTOS_RV32   2
#endif

#define MERLIN_META_MAGIC          0x564C524Du   /* 'MRLV' little-endian */
#define MERLIN_META_VERSION_MAJOR  1
#define MERLIN_META_VERSION_MINOR  0

#define MERLIN_NAME_MAX            32
#define MERLIN_TOOLCHAIN_MAX       32

/* meta flags from 02-isa-and-bytecode.md §8.3 */
#ifndef MERLIN_META_F_HAS_BTF_V
#define MERLIN_META_F_HAS_BTF_V    (1U << 0)
#define MERLIN_META_F_HAS_MAPS     (1U << 1)
#define MERLIN_META_F_HAS_RELOCS   (1U << 2)
#define MERLIN_META_F_SLEEPABLE    (1U << 8)
#define MERLIN_META_F_LARGEMEM     (1U << 9)
#define MERLIN_META_F_NEED_A_EXT   (1U << 16)
#define MERLIN_META_F_NEED_ZBB     (1U << 17)
#define MERLIN_META_F_NEED_ZBA     (1U << 18)
#define MERLIN_META_F_NEED_ZBS     (1U << 19)
#endif

/*
 * Mirror of struct merlin_meta_v1 from
 * docs/design/02-isa-and-bytecode.md §8.3.  Programs do not declare
 * this struct directly; the MERLIN_META() macro emits it.
 *
 * Total size: 80 bytes.
 */
struct merlin_meta_v1 {
	/* identity */
	__u32  magic;                /* MERLIN_META_MAGIC                  */
	__u16  version_major;        /* 1 for v1                           */
	__u16  version_minor;        /* loaders accept any v1.x            */
	__u32  meta_size;            /* sizeof(this) as emitted            */
	__u32  flags;                /* MERLIN_META_F_*                    */

	/* profile and program kind */
	__u32  bytecode_profile;     /* enum merlin_profile                */
	__u32  prog_type;            /* enum merlin_prog_type              */
	__u32  expected_attach_type;
	__u32  _reserved0;           /* MBZ                                */

	/* numeric limits the program asks the verifier to apply */
	__u32  requested_stack;      /* bytes; 0 = profile default         */
	__u32  requested_steps;      /* verifier step cap; 0 = default     */
	__u32  _reserved1[2];        /* MBZ                                */

	/* human-readable identification */
	char   prog_name[MERLIN_NAME_MAX];     /* NUL-padded               */
	char   toolchain[MERLIN_TOOLCHAIN_MAX];/* e.g. "gcc 14 riscv64-..."*/
};

/*
 * MERLIN_META(profile_constant) - emit the mandatory .merlin.meta record.
 *
 * The objtool pipeline may rewrite the `toolchain` field at link time
 * to reflect the actual compiler used; the source default is a
 * stable placeholder.
 */
#define MERLIN_META(_profile)                                                 \
	MERLIN_META_SECTION                                                   \
	const struct merlin_meta_v1 __merlin_meta = {                         \
		.magic               = MERLIN_META_MAGIC,                     \
		.version_major       = MERLIN_META_VERSION_MAJOR,             \
		.version_minor       = MERLIN_META_VERSION_MINOR,             \
		.meta_size           = sizeof(struct merlin_meta_v1),         \
		.flags               = 0,                                     \
		.bytecode_profile    = (_profile),                            \
		.prog_type           = 0,                                     \
		.expected_attach_type= 0,                                     \
		.requested_stack     = 0,                                     \
		.requested_steps     = 0,                                     \
		.prog_name           = "",                                    \
		.toolchain           = "merlin-toolchain-v1",                 \
	}

/*
 * MERLIN_META_EX() is the extended form: caller specifies prog_type,
 * expected_attach_type, and flags explicitly.  Useful when the
 * objtool pipeline cannot infer prog_type from the program-section
 * name (e.g. when SEC() is used directly without MERLIN_PROG_*).
 */
#define MERLIN_META_EX(_profile, _prog_type, _eat, _flags)                    \
	MERLIN_META_SECTION                                                   \
	const struct merlin_meta_v1 __merlin_meta = {                         \
		.magic               = MERLIN_META_MAGIC,                     \
		.version_major       = MERLIN_META_VERSION_MAJOR,             \
		.version_minor       = MERLIN_META_VERSION_MINOR,             \
		.meta_size           = sizeof(struct merlin_meta_v1),         \
		.flags               = (_flags),                              \
		.bytecode_profile    = (_profile),                            \
		.prog_type           = (_prog_type),                          \
		.expected_attach_type= (_eat),                                \
		.toolchain           = "merlin-toolchain-v1",                 \
	}

#endif /* _MERLIN_MERLIN_H */
