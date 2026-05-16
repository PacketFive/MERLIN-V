/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 * MVDP - MERLIN-V Data Path
 * AF_MVDP socket family UAPI
 *
 * Canonical design: docs/design/08-mvdp-and-af-mvdp.md
 *
 * Stability: DRAFT.  AF_MVDP, PF_MVDP and SOL_MVDP numbers are upstream
 * coordination asks; see the #ifndef guards below.  Once those numbers
 * are allocated and the RFC v1 patch series is posted, the layouts in
 * this header are frozen by the standard Linux UAPI rules.
 */
#ifndef _UAPI__LINUX_IF_MVDP_H
#define _UAPI__LINUX_IF_MVDP_H

#include <linux/types.h>
#include <linux/socket.h>

/* -------------------------------------------------------------------------
 * Address family and sockopt level
 * -------------------------------------------------------------------------
 * These numbers are TBD upstream.  Out-of-tree builds fall back to
 * AF_MAX + 1 via the #defines below.  The in-tree patch series requests
 * specific numbers from the networking maintainers.
 */
#ifndef AF_MVDP
#define AF_MVDP			(AF_MAX + 1)	/* placeholder, see above */
#define PF_MVDP			AF_MVDP
#endif

#ifndef SOL_MVDP
#define SOL_MVDP		283		/* placeholder; coordinate with
						 * include/linux/socket.h    */
#endif

/* -------------------------------------------------------------------------
 * Frame metadata visible to an MVDP program (verifier-validated)
 * -------------------------------------------------------------------------
 * Not a C type the program compiles against.  The struct describes the
 * fixed layout the verifier enforces against a0 on program entry; loads
 * and stores by the bytecode are type-checked against these offsets.
 *
 * Pointer fields (data, data_end, data_meta) are 64-bit on all profiles
 * for uniform layout.  On rv32 the high half is required to be zero.
 */
struct mvdp_md {
	__u64	data;
	__u64	data_end;
	__u64	data_meta;
	__u32	ingress_ifindex;
	__u32	rx_queue_index;
	__u32	egress_ifindex;		/* set by MVDP_REDIRECT; 0 else */
	__u32	frame_flags;		/* MVDP_FRAME_F_*               */
	__u64	hw_timestamp;		/* RX timestamp in ns; 0 if N/A */
	__u32	csum_status;		/* enum mvdp_csum_status        */
	__u32	vlan_tci;		/* 0 if not present             */
	__u32	hash;			/* RX hash; 0 if N/A            */
	__u32	_reserved;
};

#define MVDP_FRAME_F_VLAN_PRESENT	(1U << 0)
#define MVDP_FRAME_F_HASH_VALID		(1U << 1)
#define MVDP_FRAME_F_TIMESTAMP_VALID	(1U << 2)
/* bits 3..31 reserved, must read as zero */

enum mvdp_csum_status {
	MVDP_CSUM_NONE		= 0,
	MVDP_CSUM_UNNECESSARY	= 1,
	MVDP_CSUM_COMPLETE	= 2,
	MVDP_CSUM_BAD		= 3,
};

/* -------------------------------------------------------------------------
 * Program verdicts
 * -------------------------------------------------------------------------
 * Values >= 6 are treated as MVDP_ABORTED for forward compatibility.
 *
 * MVDP_DELIVER is the unified-socket model verdict: the frame goes to
 * the AF_MVDP socket that installed this program.  MVDP_REDIRECT is
 * the explicit-target verdict (DEVMAP / CPUMAP / MVSKMAP).
 */
enum mvdp_action {
	MVDP_ABORTED	= 0,
	MVDP_DROP	= 1,
	MVDP_PASS	= 2,
	MVDP_TX		= 3,
	MVDP_REDIRECT	= 4,
	MVDP_DELIVER	= 5,

	__MAX_MVDP_ACTION
};

/* -------------------------------------------------------------------------
 * Install-mode flags
 * -------------------------------------------------------------------------
 * Used both in struct mvdp_prog_attach (unified-socket install) and in
 * link_create.flags when attach_type == MERLIN_ATTACH_MVDP (headless
 * install).  Exactly one of the *_MODE flags MUST be set.
 *
 * RFC v1 implements SKB_MODE only.  DRV_MODE and HW_MODE are
 * reserved (see 08-mvdp-and-af-mvdp.md §2.4).
 */
#define MVDP_FLAGS_SKB_MODE		(1U << 0)
#define MVDP_FLAGS_DRV_MODE		(1U << 1)
#define MVDP_FLAGS_HW_MODE		(1U << 2)
#define MVDP_INSTALL_F_HEADLESS		(1U << 3)
#define MVDP_INSTALL_F_REPLACE		(1U << 4)
#define MVDP_FLAGS_MODES_MASK		(MVDP_FLAGS_SKB_MODE | \
					 MVDP_FLAGS_DRV_MODE | \
					 MVDP_FLAGS_HW_MODE)

/* -------------------------------------------------------------------------
 * UMEM (the userspace shared frame pool)
 * -------------------------------------------------------------------------
 */
struct mvdp_umem_reg {
	__u64	addr;			/* userspace vaddr of the area    */
	__u64	len;			/* length in bytes                */
	__u32	chunk_size;		/* per-frame chunk size           */
	__u32	headroom;		/* reserved bytes at start of each chunk */
	__u32	flags;			/* MVDP_UMEM_F_*                  */
	__u32	tx_metadata_len;	/* per-frame TX metadata; 0 if N/A*/
};

#define MVDP_UMEM_F_UNALIGNED_CHUNK_FLAG	(1U << 0)
/* bits 1..31 reserved */

/* -------------------------------------------------------------------------
 * Ring descriptor (RX/TX rings)
 * -------------------------------------------------------------------------
 * Bit-compatible with struct xdp_desc; this is a deliberate choice so
 * existing zero-copy userspace libraries can port mechanically.  See
 * 08-mvdp-and-af-mvdp.md §3.5.
 */
struct mvdp_desc {
	__u64	addr;			/* offset into UMEM (or unaligned addr) */
	__u32	len;			/* frame length in bytes                */
	__u32	options;		/* MVDP_DESC_OPT_*                      */
};

#define MVDP_DESC_OPT_RX_HASH_VALID	(1U << 0)
#define MVDP_DESC_OPT_RX_TS_VALID	(1U << 1)
/* bits 2..31 reserved */

/* -------------------------------------------------------------------------
 * Bind address (sockaddr_mvdp)
 * -------------------------------------------------------------------------
 */
struct sockaddr_mvdp {
	__kernel_sa_family_t	sa_family;	/* AF_MVDP            */
	__u16			_pad0;
	__u32			ifindex;
	__u32			queue_id;
	__u32			flags;		/* MVDP_BIND_F_*      */
	__u32			shared_umem_fd;	/* 0 if not sharing   */
	__u32			_reserved;
};

#define MVDP_BIND_F_SHARED_UMEM		(1U << 0)
#define MVDP_BIND_F_ZEROCOPY		(1U << 1)
#define MVDP_BIND_F_NEEDS_WAKEUP	(1U << 2)
/* bits 3..31 reserved */

/* -------------------------------------------------------------------------
 * Socket options under SOL_MVDP
 * -------------------------------------------------------------------------
 *
 * Layout: setsockopt-only values 1..15, getsockopt-only 16..31, and
 *         setsockopt for program control 20..31 (overlaps deliberate;
 *         these have separate setsockopt and getsockopt semantics).
 */

/* Data-plane configuration (setsockopt only) */
#define MVDP_UMEM_REG			1
#define MVDP_UMEM_FILL_RING		2
#define MVDP_UMEM_COMPLETION_RING	3
#define MVDP_RX_RING			4
#define MVDP_TX_RING			5

/* Introspection (getsockopt only) */
#define MVDP_STATISTICS			16
#define MVDP_OPTIONS			17
#define MVDP_RING_OFFSETS		18
#define MVDP_MMAP_OFFSETS		19

/* Program control (setsockopt for ATTACH/DETACH, getsockopt for QUERY) */
#define MVDP_PROG_ATTACH		20
#define MVDP_PROG_DETACH		21
#define MVDP_PROG_QUERY			22

/* -------------------------------------------------------------------------
 * Program control payloads
 * -------------------------------------------------------------------------
 */
enum mvdp_deliver_mode {
	MVDP_DELIVER_DEFAULT	= 0,	/* MVDP_DELIVER -> this socket's RX */
	MVDP_DELIVER_VIA_MAP	= 1,	/* program must use MVSKMAP        */
	MVDP_DELIVER_NONE	= 2,	/* MVDP_DELIVER is rejected by ver */

	__MAX_MVDP_DELIVER_MODE
};

struct mvdp_prog_attach {
	__u32	prog_fd;		/* fd from MERLIN_PROG_LOAD            */
	__u32	install_flags;		/* MVDP_FLAGS_{SKB,DRV,HW}_MODE + ...  */
	__u32	deliver_mode;		/* enum mvdp_deliver_mode              */
	__u32	current_revision;	/* OUT (for QUERY); IN for ATTACH:
					 * if non-zero, this is a CAS guard
					 * for atomic in-place replace        */
	__u32	new_revision;		/* OUT: revision after ATTACH succeeds */
	__u32	_pad;
};

struct mvdp_statistics {
	__u64	rx_dropped;
	__u64	rx_invalid_descs;
	__u64	tx_invalid_descs;
	__u64	rx_ring_full;
	__u64	rx_fill_ring_empty_descs;
	__u64	tx_ring_empty_descs;
};

struct mvdp_ring_offsets {
	__u64	producer;
	__u64	consumer;
	__u64	desc;
	__u64	flags;
};

struct mvdp_mmap_offsets {
	struct mvdp_ring_offsets rx;
	struct mvdp_ring_offsets tx;
	struct mvdp_ring_offsets fr;	/* fill ring       */
	struct mvdp_ring_offsets cr;	/* completion ring */
};

/* Magic offsets passed to mmap() to obtain each ring. */
#define MVDP_PGOFF_RX_RING		0x000000000ULL
#define MVDP_PGOFF_TX_RING		0x080000000ULL
#define MVDP_UMEM_PGOFF_FILL_RING	0x100000000ULL
#define MVDP_UMEM_PGOFF_COMPLETION_RING	0x180000000ULL

#endif /* _UAPI__LINUX_IF_MVDP_H */
