/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * merlin_internal.h - in-kernel types for MERLIN-V.
 *
 * This header is kernel-internal only.  The user-visible types live in
 * docs/design/uapi/linux/merlin.h (and will be lifted to
 * include/uapi/linux/merlin.h in the RFC patch series).
 *
 * Cross-references:
 *   docs/design/03-kernel-interfaces.md   Syscall, hooks, maps, caps.
 *   docs/design/06-verifier.md            Verifier strategy and domains.
 *   docs/design/07-jit-and-offload.md     JIT architecture.
 */
#ifndef _MERLIN_INTERNAL_H
#define _MERLIN_INTERNAL_H

#include <linux/atomic.h>
#include <linux/file.h>
#include <linux/idr.h>
#include <linux/list.h>
#include <linux/mutex.h>
#include <linux/refcount.h>
#include <linux/spinlock.h>
#include <linux/types.h>
#include <linux/workqueue.h>

/* Pull in the draft UAPI types we reference here.
 * In-tree path: include/uapi/linux/merlin.h
 * Out-of-tree:  docs/design/uapi/linux/merlin.h (via -I ccflag in Makefile)
 */
#include <uapi/linux/merlin.h>

/* -----------------------------------------------------------------------
 * Forward declarations
 * ----------------------------------------------------------------------- */
struct merlin_prog;
struct merlin_map;
struct merlin_link;

/* -----------------------------------------------------------------------
 * Limits and tuning knobs
 * ----------------------------------------------------------------------- */
#define MERLIN_MAX_ELF_SIZE	(16 * 1024 * 1024)   /* 16 MiB ELF blob cap  */
#define MERLIN_MAX_INSNS	(1024 * 1024)         /* 4 MiB text cap       */
#define MERLIN_LOG_BUF_MAX	(16 * 1024 * 1024)   /* 16 MiB verifier log  */
#define MERLIN_PROG_TAG_LEN	8                    /* 8-byte SHA-256 prefix */
#define MERLIN_MAX_HELPER_ID	4095
#define MERLIN_STACK_SIZE_DEFAULT	512          /* bytes, RV64 default  */
#define MERLIN_STACK_SIZE_SLEEPABLE	8192
#define MERLIN_STACK_SIZE_RTOS		256          /* RV32 Zephyr target   */

/* -----------------------------------------------------------------------
 * JIT image lifecycle
 *
 * A jit_image represents a region of vmalloc'd exec memory containing
 * the translated native code.  RCU is used for updates (program
 * hot-swap): the old image is freed after an RCU grace period.
 * ----------------------------------------------------------------------- */
struct merlin_jit_image {
	void    *image;          /* vmalloc'd RX region                       */
	u32      image_len;      /* allocated length                          */
	u32      code_len;       /* useful bytes                              */
	struct   rcu_head rcu;   /* for RCU-safe free                         */
};

typedef u64 (*merlin_prog_fn)(void *ctx);

/* -----------------------------------------------------------------------
 * merlin_prog — kernel representation of a loaded program
 *
 * Lifecycle: created on MERLIN_PROG_LOAD, destroyed when the last
 * reference (fd or link) is dropped.
 *
 * Locking: mostly immutable after creation.  jit_image may be swapped
 * atomically via MERLIN_LINK_UPDATE; use rcu_read_lock / rcu_dereference.
 * ----------------------------------------------------------------------- */
struct merlin_prog {
	/* Identity */
	u32          id;              /* IDR-assigned, stable across fd close   */
	u8           tag[MERLIN_PROG_TAG_LEN]; /* SHA-256 prefix of bytecode    */
	char         name[MERLIN_OBJ_NAME_LEN];
	u64          load_time_ns;

	/* Bytecode (immutable after load) */
	u8          *bytecode;        /* kmalloc'd copy of .text section        */
	u32          bytecode_len;

	/* Type / profile (immutable) */
	enum merlin_prog_type   prog_type;
	enum merlin_attach_type expected_attach_type;
	enum merlin_profile     profile;
	u32          prog_flags;

	/* JIT image (may be updated; protected by RCU) */
	struct merlin_jit_image __rcu *jit_image;
	merlin_prog_fn  prog_fn;     /* == jit_image->image; cached for fast call */

	/* Runtime stats (updated atomically by prog invocation path) */
	atomic64_t   run_cnt;
	atomic64_t   run_time_ns;
	atomic64_t   recursion_misses;

	/* Ownership and reference counting */
	refcount_t   refs;
	uid_t        created_by_uid;
	u32          gpl_compatible:1;

	struct work_struct  work_free; /* deferred free via WQ (GFP constraints) */
};

/* -----------------------------------------------------------------------
 * merlin_map — map handle (wraps struct bpf_map)
 *
 * MERLIN-V reuses kernel/bpf/ map infrastructure, so this is a very thin
 * wrapper that lets us issue MERLIN_MAP_* commands while the underlying
 * storage, locking and RCU discipline remain in struct bpf_map.
 *
 * See docs/design/03-kernel-interfaces.md §5.
 * ----------------------------------------------------------------------- */
struct merlin_map {
	u32               id;
	char              name[MERLIN_OBJ_NAME_LEN];
	struct bpf_map   *bpf_map;   /* backing bpf_map                       */
	refcount_t        refs;
};

/* -----------------------------------------------------------------------
 * merlin_link — attachment of a program to a hook point
 *
 * Mirrors struct bpf_link.  We keep a thin parallel structure rather than
 * embedding in struct bpf_link so that MERLIN-V-specific fields don't
 * pollute BPF structures.  In the long run this can be unified.
 * ----------------------------------------------------------------------- */
struct merlin_link {
	u32               id;
	enum merlin_attach_type attach_type;
	struct merlin_prog     *prog;   /* refcounted                          */
	refcount_t              refs;
};

/* -----------------------------------------------------------------------
 * Global ID registries
 *
 * One IDR per object type, guarded by a spinlock (same pattern as BPF).
 * ----------------------------------------------------------------------- */
extern struct idr merlin_prog_idr;
extern struct idr merlin_map_idr;
extern struct idr merlin_link_idr;
extern spinlock_t merlin_idr_lock;

/* -----------------------------------------------------------------------
 * Verifier API (verifier.c / decode.c)
 * ----------------------------------------------------------------------- */

/* Abstract register-value domain (mirroring tools/merlin-verifier/verify.h) */
enum merlin_rval_kind {
	RVAL_UNKNOWN = 0,
	RVAL_CONST,
	RVAL_PTR_CTX,
	RVAL_PTR_STACK,
	RVAL_PTR_HELPER_RET,
};

struct merlin_rval {
	enum merlin_rval_kind kind;
	u64 val;      /* CONST: value | PTR: offset                            */
	u32 extra;    /* PTR_HELPER_RET: helper id                             */
};

struct merlin_vstate {
	struct merlin_rval x[32];  /* one abstract value per RV register       */
};

struct merlin_verifier_cfg {
	u8   helper_allow[MERLIN_MAX_HELPER_ID / 8 + 1]; /* bitset            */
	u32  max_stack_bytes;
	bool allow_back_edges;
	bool verbose;
	char *log_buf;
	u32   log_buf_sz;
	u32   log_pos;
};

enum merlin_verify_result {
	MERLIN_VERIFY_OK     = 0,
	MERLIN_VERIFY_REJECT = 1,
};

enum merlin_verify_result merlin_verify_text(
	const u8 *text, size_t text_size, u32 text_offset,
	const struct merlin_verifier_cfg *cfg);

void merlin_verifier_cfg_for_prog(struct merlin_verifier_cfg *cfg,
				  enum merlin_prog_type prog_type,
				  u32 prog_flags);

/* -----------------------------------------------------------------------
 * Instruction decoder API (decode.c)
 *
 * Kernel port of tools/merlin-verifier/decode.h.
 * ----------------------------------------------------------------------- */
enum merlin_insn_class {
	INSN_INVALID = 0,
	INSN_UNSUPPORTED,
	INSN_LUI, INSN_AUIPC,
	INSN_JAL, INSN_JALR,
	INSN_BRANCH,
	INSN_LOAD, INSN_STORE,
	INSN_ALU_IMM, INSN_SHIFT_IMM,
	INSN_ALU_REG,
	INSN_FENCE,
	INSN_ECALL, INSN_EBREAK,
	INSN_ALU_IMM_W, INSN_ALU_REG_W,
	INSN_MUL, INSN_DIV,
	/* forbidden - recognised for rejection */
	INSN_CSR, INSN_PRIV, INSN_FENCE_I, INSN_FLOAT,
};

enum merlin_branch_kind { BR_BEQ, BR_BNE, BR_BLT, BR_BGE, BR_BLTU, BR_BGEU };
enum merlin_load_kind   { LD_LB=0, LD_LH=1, LD_LW=2, LD_LD=3,
			  LD_LBU=4, LD_LHU=5, LD_LWU=6 };
enum merlin_store_kind  { ST_SB=0, ST_SH=1, ST_SW=2, ST_SD=3 };
enum merlin_alu_op {
	ALU_ADD, ALU_SUB, ALU_SLL, ALU_SLT, ALU_SLTU,
	ALU_XOR, ALU_SRL, ALU_SRA, ALU_OR,  ALU_AND,
	ALU_MUL, ALU_DIV, ALU_DIVU, ALU_REM, ALU_REMU,
	ALU_MULH, ALU_MULHU, ALU_MULHSU,
};

struct merlin_insn {
	u32  raw;
	u32  pc;
	enum merlin_insn_class  cls;
	u8   rd, rs1, rs2;
	s64  imm;
	enum merlin_branch_kind br_kind;
	enum merlin_load_kind   ld_kind;
	enum merlin_store_kind  st_kind;
	enum merlin_alu_op      alu_op;
	bool word_op;
};

int  merlin_decode(u32 insn_word, u32 pc, struct merlin_insn *out);
bool merlin_insn_permitted_default(const struct merlin_insn *in);
const char *merlin_insn_class_name(enum merlin_insn_class cls);

/* -----------------------------------------------------------------------
 * JIT operations table
 *
 * Each supported host ISA registers a merlin_jit_ops; the loader picks
 * the correct one at prog_load time.  On RISC-V the pass-through ops
 * are selected; on x86_64 the x86_64 ops.
 * ----------------------------------------------------------------------- */
struct merlin_jit_ops {
	const char *name;
	int (*translate)(const u8 *text, size_t text_len,
			 struct merlin_prog *prog,
			 struct merlin_jit_image **img_out);
	void (*free_image)(struct merlin_jit_image *img);
};

extern const struct merlin_jit_ops merlin_jit_passthrough;
extern const struct merlin_jit_ops merlin_jit_x86_64;

const struct merlin_jit_ops *merlin_select_jit(enum merlin_profile profile);

/* -----------------------------------------------------------------------
 * Helper / dispatch (dispatch.c)
 * ----------------------------------------------------------------------- */
typedef u64 (*merlin_helper_fn)(u64 a0, u64 a1, u64 a2, u64 a3, u64 a4, u64 a5);

struct merlin_helper_entry {
	u32                 id;
	const char         *name;
	merlin_helper_fn    fn;
	bool                gpl_only;
};

u64 merlin_dispatch_helper(u32 id, u64 a0, u64 a1, u64 a2,
			   u64 a3, u64 a4, u64 a5);

/* -----------------------------------------------------------------------
 * Loader API (loader.c)
 * ----------------------------------------------------------------------- */
int merlin_prog_load(const union merlin_attr __user *uattr, u32 attr_sz,
		     struct merlin_prog **prog_out);

/* -----------------------------------------------------------------------
 * Lifecycle helpers (core.c)
 * ----------------------------------------------------------------------- */
void merlin_prog_put(struct merlin_prog *prog);
struct merlin_prog *merlin_prog_get_by_id(u32 id);
struct merlin_prog *merlin_prog_get_by_fd(int fd);

int merlin_prog_new_fd(struct merlin_prog *prog);
int merlin_prog_alloc_id(struct merlin_prog *prog);

/* -----------------------------------------------------------------------
 * Map helpers (maps.c)
 * ----------------------------------------------------------------------- */
int merlin_map_create(const union merlin_attr __user *uattr, u32 attr_sz);

/* -----------------------------------------------------------------------
 * Logging helpers (used by verifier and loader)
 * ----------------------------------------------------------------------- */
static inline void merlin_log(struct merlin_verifier_cfg *cfg,
			      const char *fmt, ...)
{
	va_list ap;

	if (!cfg->log_buf || cfg->log_pos >= cfg->log_buf_sz)
		return;
	va_start(ap, fmt);
	cfg->log_pos += vscnprintf(cfg->log_buf + cfg->log_pos,
				   cfg->log_buf_sz - cfg->log_pos, fmt, ap);
	va_end(ap);
}

#endif /* _MERLIN_INTERNAL_H */
