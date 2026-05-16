/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 * MERLIN-V: in-kernel JIT VM with a RISC-V instruction set.
 *
 * This is the draft UAPI header.  It is the canonical source for the
 * union merlin_attr layout, command numbers, profile / program / map
 * / link enumerations, and prog-load flag bits.  It will be lifted
 * into include/uapi/linux/merlin.h in the kernel patch series with
 * no further changes (modulo formatting).
 *
 * Cross-references:
 *   docs/design/02-isa-and-bytecode.md   ELF object format,
 *                                        profile names, helper ABI.
 *   docs/design/03-kernel-interfaces.md  Syscall, hooks, maps,
 *                                        capabilities, namespacing.
 *   docs/design/06-verifier.md           Verifier profiles and the
 *                                        prog_flags interactions.
 *   docs/design/08-mvdp-and-af-mvdp.md   MVDP program type and the
 *                                        AF_MVDP socket family.
 *
 * Stability: DRAFT.  Anything in this header may change before the
 * RFC v1 patch series is posted.  Once posted, the layout is frozen
 * by the standard Linux UAPI rules.
 */
#ifndef _UAPI__LINUX_MERLIN_H
#define _UAPI__LINUX_MERLIN_H

#include <linux/types.h>

#define MERLIN_OBJ_NAME_LEN	16

/* -------------------------------------------------------------------------
 * Syscall: int merlin(enum merlin_cmd cmd, union merlin_attr *uattr, u32 sz)
 * -------------------------------------------------------------------------
 */

enum merlin_cmd {
	MERLIN_PROG_LOAD		= 0,
	MERLIN_PROG_TEST_RUN		= 1,
	MERLIN_PROG_GET_INFO_BY_FD	= 2,
	MERLIN_PROG_GET_NEXT_ID		= 3,
	MERLIN_PROG_GET_FD_BY_ID	= 4,

	MERLIN_MAP_CREATE		= 5,
	MERLIN_MAP_LOOKUP_ELEM		= 6,
	MERLIN_MAP_UPDATE_ELEM		= 7,
	MERLIN_MAP_DELETE_ELEM		= 8,
	MERLIN_MAP_GET_NEXT_KEY		= 9,
	MERLIN_MAP_GET_FD_BY_ID		= 10,
	MERLIN_MAP_GET_INFO_BY_FD	= 11,

	MERLIN_LINK_CREATE		= 12,
	MERLIN_LINK_UPDATE		= 13,
	MERLIN_LINK_DETACH		= 14,
	MERLIN_LINK_GET_INFO_BY_FD	= 15,

	MERLIN_BTF_LOAD			= 16,
	MERLIN_BTF_GET_FD_BY_ID		= 17,

	MERLIN_OBJ_PIN			= 18,
	MERLIN_OBJ_GET			= 19,

	/* MVCP control plane; design: 09-mvcp-kernel-uapi.md */
	MERLIN_PROG_GET_ATTESTATION	= 20,
	MERLIN_MAP_BATCH_TXN_BEGIN	= 21,
	MERLIN_MAP_BATCH_TXN_STAGE	= 22,
	MERLIN_MAP_BATCH_TXN_COMMIT	= 23,
	MERLIN_MAP_BATCH_TXN_ABORT	= 24,
	MERLIN_NS_CREATE		= 25,
	MERLIN_NS_GET_FD_BY_ID		= 26,
	MERLIN_KEYRING_BIND		= 27,	/* trust-root for signed progs */

	__MAX_MERLIN_CMD
};

/* -------------------------------------------------------------------------
 * Bytecode profile (declared in .merlin.meta of the ELF blob)
 * -------------------------------------------------------------------------
 * See docs/design/02-isa-and-bytecode.md §1.
 */
enum merlin_profile {
	MERLIN_PROFILE_UNSPEC		= 0,
	MERLIN_PROFILE_LINUX_RV64	= 1,	/* rv64imac_zicsr_zifencei  */
	MERLIN_PROFILE_RTOS_RV32	= 2,	/* rv32imc_zicsr_zifencei   */

	__MAX_MERLIN_PROFILE
};

/* -------------------------------------------------------------------------
 * Program types
 * -------------------------------------------------------------------------
 * Each entry defines a contract: a fixed-layout ctx struct, a verifier
 * profile (see 06-verifier.md §7), and an allowed helper set.
 *
 * _V suffix denotes "MERLIN-V variant of an existing eBPF program type" -
 * the program shares the kernel hook surface but runs MERLIN-V bytecode.
 *
 * MVDP has no _V suffix because it is a MERLIN-V-native data path with
 * its own ctx, verdicts, and socket family (08-mvdp-and-af-mvdp.md).
 */
enum merlin_prog_type {
	MERLIN_PROG_TYPE_UNSPEC			= 0,
	MERLIN_PROG_TYPE_SOCKET_FILTER_V	= 1,
	MERLIN_PROG_TYPE_KPROBE_V		= 2,
	MERLIN_PROG_TYPE_TRACEPOINT_V		= 3,
	MERLIN_PROG_TYPE_PERF_EVENT_V		= 4,
	MERLIN_PROG_TYPE_XDP_V			= 5,
	MERLIN_PROG_TYPE_TC_V			= 6,
	MERLIN_PROG_TYPE_MVDP			= 7,

	__MAX_MERLIN_PROG_TYPE
};

/* -------------------------------------------------------------------------
 * Attach types (selected at MERLIN_LINK_CREATE)
 * -------------------------------------------------------------------------
 * A single prog_type may support multiple attach_types (e.g. TC_V has
 * INGRESS and EGRESS).  The verifier picks a profile based on the
 * (prog_type, attach_type) pair.
 */
enum merlin_attach_type {
	MERLIN_ATTACH_NONE		= 0,
	MERLIN_ATTACH_MVDP		= 1,
	MERLIN_ATTACH_XDP_V		= 2,
	MERLIN_ATTACH_TC_INGRESS	= 3,
	MERLIN_ATTACH_TC_EGRESS		= 4,
	MERLIN_ATTACH_KPROBE		= 5,
	MERLIN_ATTACH_KRETPROBE		= 6,
	MERLIN_ATTACH_TRACEPOINT	= 7,
	MERLIN_ATTACH_PERF_EVENT	= 8,
	MERLIN_ATTACH_SOCKET_FILTER	= 9,

	__MAX_MERLIN_ATTACH_TYPE
};

/* -------------------------------------------------------------------------
 * Map types
 * -------------------------------------------------------------------------
 * Numbers correspond 1:1 with the matching BPF_MAP_TYPE_* where the
 * semantic is identical; the kernel-side map paths delegate to
 * struct bpf_map_ops (see 03-kernel-interfaces.md §5).
 *
 * MVSKMAP is MERLIN-V specific (AF_MVDP socket redirect).
 */
enum merlin_map_type {
	MERLIN_MAP_TYPE_UNSPEC		= 0,
	MERLIN_MAP_TYPE_HASH		= 1,
	MERLIN_MAP_TYPE_ARRAY		= 2,
	MERLIN_MAP_TYPE_PERCPU_HASH	= 3,
	MERLIN_MAP_TYPE_PERCPU_ARRAY	= 4,
	MERLIN_MAP_TYPE_LRU_HASH	= 5,
	MERLIN_MAP_TYPE_LPM_TRIE	= 6,
	MERLIN_MAP_TYPE_RINGBUF		= 7,
	MERLIN_MAP_TYPE_PROG_ARRAY	= 8,	/* tail calls       */
	MERLIN_MAP_TYPE_XSKMAP		= 9,	/* AF_XDP redirect  */
	MERLIN_MAP_TYPE_MVSKMAP		= 10,	/* AF_MVDP redirect */

	__MAX_MERLIN_MAP_TYPE
};

/* -------------------------------------------------------------------------
 * Log levels for verifier output (returned in log_buf)
 * -------------------------------------------------------------------------
 */
#define MERLIN_LOG_LEVEL_NONE		0
#define MERLIN_LOG_LEVEL_BRIEF		1
#define MERLIN_LOG_LEVEL_VERBOSE	2
#define MERLIN_LOG_LEVEL_STATS		4	/* may be OR'd with above */

/* -------------------------------------------------------------------------
 * Program-load flags (prog_load.prog_flags)
 * -------------------------------------------------------------------------
 */
#define MERLIN_F_STRICT_ALIGNMENT	(1U << 0)
#define MERLIN_F_ANY_ALIGNMENT		(1U << 1)
#define MERLIN_F_TEST_RND_HI32		(1U << 2)
#define MERLIN_F_SLEEPABLE		(1U << 4)
#define MERLIN_F_XDP_HAS_FRAGS		(1U << 5)   /* XDP_V multi-buf  */
#define MERLIN_F_MVDP_HAS_FRAGS		(1U << 6)   /* MVDP multi-buf   */

/* -------------------------------------------------------------------------
 * Map flags (map_create.map_flags)
 * -------------------------------------------------------------------------
 */
#define MERLIN_MAP_F_NO_PREALLOC	(1U << 0)
#define MERLIN_MAP_F_NO_COMMON_LRU	(1U << 1)
#define MERLIN_MAP_F_NUMA_NODE		(1U << 2)
#define MERLIN_MAP_F_RDONLY		(1U << 3)
#define MERLIN_MAP_F_WRONLY		(1U << 4)
#define MERLIN_MAP_F_RDONLY_PROG	(1U << 7)
#define MERLIN_MAP_F_WRONLY_PROG	(1U << 8)
#define MERLIN_MAP_F_INNER_MAP		(1U << 12)

/* -------------------------------------------------------------------------
 * Map update flags (map_elem.flags for UPDATE_ELEM)
 * -------------------------------------------------------------------------
 */
#define MERLIN_ANY	0	/* create new element or update existing */
#define MERLIN_NOEXIST	1	/* create new element only if not exists */
#define MERLIN_EXIST	2	/* update existing element only          */
#define MERLIN_F_LOCK	4

/* -------------------------------------------------------------------------
 * The big multiplexer
 * -------------------------------------------------------------------------
 * One struct per command.  Layout is fixed by UAPI rules; new fields
 * may only be appended in zero-initialised tail regions, and the
 * kernel must accept any size >= the size it understands and zero
 * the tail.  This is the same compatibility rule the bpf(2) syscall
 * follows.
 */
union merlin_attr {

	/* --- MERLIN_PROG_LOAD --- */
	struct {
		__aligned_u64	elf_ptr;	   /* user ptr to ELF blob       */
		__u32		elf_len;
		__u32		prog_type;	   /* enum merlin_prog_type      */
		__u32		expected_attach_type;
		__u32		profile;	   /* enum merlin_profile        */
		__u32		log_level;	   /* MERLIN_LOG_LEVEL_*         */
		__aligned_u64	log_buf;
		__u32		log_size;
		__u32		prog_flags;	   /* MERLIN_F_*                 */
		char		prog_name[MERLIN_OBJ_NAME_LEN];
		__aligned_u64	license_ptr;	   /* user ptr to license string */
		__u32		prog_btf_fd;	   /* MERLIN BTF fd, 0 if none   */
		__u32		_pad0;
		__aligned_u64	func_info;	   /* per-function info array    */
		__u32		func_info_rec_size;
		__u32		func_info_cnt;
		__aligned_u64	line_info;	   /* line table for verifier    */
		__u32		line_info_rec_size;
		__u32		line_info_cnt;
		__u32		fd_array_uref_cnt; /* number of referenced FDs   */
		__u32		_pad1;
		__aligned_u64	fd_array;	   /* user ptr to __u32 fd[]     */
	} prog_load;

	/* --- MERLIN_MAP_CREATE --- */
	struct {
		__u32		map_type;	   /* enum merlin_map_type       */
		__u32		key_size;
		__u32		value_size;
		__u32		max_entries;
		__u32		map_flags;	   /* MERLIN_MAP_F_*             */
		char		map_name[MERLIN_OBJ_NAME_LEN];
		__u32		numa_node;
		__u32		map_ifindex;	   /* offload to netdev if !=0   */
		__u32		btf_fd;
		__u32		btf_key_type_id;
		__u32		btf_value_type_id;
		__u32		inner_map_fd;	   /* for map-in-map             */
		__u32		_pad;
	} map_create;

	/* --- MERLIN_MAP_{LOOKUP,UPDATE,DELETE,GET_NEXT}_ELEM --- */
	struct {
		__u32		map_fd;
		__u32		_pad;
		__aligned_u64	key;
		union {
			__aligned_u64	value;
			__aligned_u64	next_key;
		};
		__u64		flags;		   /* MERLIN_{ANY,NOEXIST,EXIST,F_LOCK} */
	} map_elem;

	/* --- MERLIN_LINK_CREATE --- */
	struct {
		__u32		prog_fd;
		__u32		target_fd;	   /* netdev fd, cgroup fd, ... */
		__u32		attach_type;	   /* enum merlin_attach_type   */
		__u32		flags;		   /* per-attach-type flag space*/
		__u32		target_ifindex;
		__u32		target_queue;
		__aligned_u64	target_btf_id;	   /* tracing attach target     */
		__u64		expected_revision; /* CAS guard, 0 = any        */
	} link_create;

	/* --- MERLIN_PROG_TEST_RUN --- */
	struct {
		__u32		prog_fd;
		__u32		retval;		   /* OUT */
		__u32		data_size_in;
		__u32		data_size_out;
		__aligned_u64	data_in;
		__aligned_u64	data_out;
		__u32		repeat;
		__u32		duration_ns;	   /* OUT */
		__u32		ctx_size_in;
		__u32		ctx_size_out;
		__aligned_u64	ctx_in;
		__aligned_u64	ctx_out;
		__u32		flags;
		__u32		cpu;
		__u32		batch_size;
		__u32		_pad;
	} test_run;

	/* --- MERLIN_BTF_LOAD --- */
	struct {
		__aligned_u64	btf;
		__aligned_u64	btf_log_buf;
		__u32		btf_size;
		__u32		btf_log_size;
		__u32		btf_log_level;
		__u32		_pad;
	} btf_load;

	/* --- MERLIN_{PROG,MAP,BTF,LINK}_GET_INFO_BY_FD --- */
	struct {
		__u32		fd;
		__u32		info_len;
		__aligned_u64	info;
	} get_info;

	/* --- MERLIN_{PROG,MAP,BTF,LINK}_GET_{NEXT_ID,FD_BY_ID} --- */
	struct {
		union {
			__u32	start_id;
			__u32	id;
		};
		__u32		next_id;	   /* OUT */
		__u32		open_flags;
		__u32		_pad;
	} get_id;

	/* --- MERLIN_OBJ_PIN / MERLIN_OBJ_GET --- */
	struct {
		__aligned_u64	pathname;	   /* user ptr to NUL string */
		__u32		fd;
		__u32		file_flags;	   /* O_RDONLY, O_RDWR, ...  */
	} pin;

} __attribute__((aligned(8)));

/* -------------------------------------------------------------------------
 * Information structs returned by MERLIN_*_GET_INFO_BY_FD
 * -------------------------------------------------------------------------
 */

struct merlin_prog_info {
	__u32	type;
	__u32	id;
	__u8	tag[8];				/* SHA-256 truncated, 8B    */
	__u32	jited_prog_len;
	__u32	xlated_prog_len;
	__aligned_u64	jited_prog_insns;
	__aligned_u64	xlated_prog_insns;
	__u64	load_time;			/* ns since boot            */
	__u32	created_by_uid;
	__u32	nr_map_ids;
	__aligned_u64	map_ids;
	char	name[MERLIN_OBJ_NAME_LEN];
	__u32	ifindex;			/* if offloaded             */
	__u32	gpl_compatible:1;
	__u32	_pad:31;
	__u64	netns_dev;
	__u64	netns_ino;
	__u32	nr_jited_ksyms;
	__u32	nr_jited_func_lens;
	__aligned_u64	jited_ksyms;
	__aligned_u64	jited_func_lens;
	__u32	btf_id;
	__u32	func_info_rec_size;
	__aligned_u64	func_info;
	__u32	nr_func_info;
	__u32	nr_line_info;
	__aligned_u64	line_info;
	__u32	line_info_rec_size;
	__u32	run_time_ns;			/* aggregate                */
	__u64	run_cnt;
	__u64	recursion_misses;
	__u32	verified_insns;
	__u32	profile;			/* enum merlin_profile      */
};

struct merlin_map_info {
	__u32	type;
	__u32	id;
	__u32	key_size;
	__u32	value_size;
	__u32	max_entries;
	__u32	map_flags;
	char	name[MERLIN_OBJ_NAME_LEN];
	__u32	ifindex;
	__u32	btf_vmlinux_value_type_id;
	__u64	netns_dev;
	__u64	netns_ino;
	__u32	btf_id;
	__u32	btf_key_type_id;
	__u32	btf_value_type_id;
	__u32	_pad;
};

struct merlin_link_info {
	__u32	type;
	__u32	id;
	__u32	prog_id;
	union {
		struct {
			__u32	ifindex;
			__u32	queue_id;
			__u32	attach_flags;	/* MVDP_FLAGS_* / etc. */
		} mvdp;
		struct {
			__u32	ifindex;
			__u32	attach_flags;
		} xdp_v;
		struct {
			__u32	ifindex;
			__u32	attach_type;
			__u32	parent;
			__u32	handle;
		} tc_v;
		struct {
			__u32	pf_type;
			__u32	cookie;
			__aligned_u64	addr;
		} kprobe;
		struct {
			__aligned_u64	tp_name;
			__u32	tp_name_len;
		} tracepoint;
		struct {
			__u64	cookie;
		} perf_event;
	};
};

#endif /* _UAPI__LINUX_MERLIN_H */
