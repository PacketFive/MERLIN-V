// SPDX-License-Identifier: GPL-2.0-only
/*
 * jit/arch/x86_64.c — MERLIN-V (RV64) → x86_64 single-pass host JIT.
 *
 * Kernel-C port of tools/merlin-jit-x86_64/{emit.c,jit.c}.
 *
 * Strategy: spill-everything.  The 32 RISC-V architectural registers live
 * as 64-bit slots in a regs[32] array on the JIT'd function's stack frame.
 * rbx holds &regs[0] throughout.  Each RV instruction translates to a
 * short fixed sequence: load operands from regs[], compute, store result.
 *
 * JIT'd function signature: u64 fn(void *ctx)
 *
 * Prologue sets regs[10] (a0) = ctx (rdi), zeroes remaining regs, and
 * establishes rbx = &regs[0].  Epilogue returns regs[10] in rax.
 *
 * Helper calls (ecall) pass &regs[0] in rdi to merlin_dispatch_helper_asm
 * (a thin asm trampoline that reads a7/a0-a5 and calls merlin_dispatch_helper).
 *
 * Cross-reference: docs/design/07-jit-and-offload.md §4.1 (x86_64 JIT).
 */

#include <linux/module.h>
#include <linux/slab.h>
#include <linux/types.h>
#include <linux/vmalloc.h>

#ifdef CONFIG_X86_64
#include <asm/cacheflush.h>
#include <asm/set_memory.h>
#endif

#include "../../include/merlin_internal.h"

/* -----------------------------------------------------------------------
 * Dynamic byte buffer (kernel-side emit buffer)
 * ----------------------------------------------------------------------- */
struct kbuf {
	u8    *buf;
	size_t len;
	size_t cap;
};

static int kbuf_init(struct kbuf *b, size_t initial)
{
	b->buf = kmalloc(initial, GFP_KERNEL);
	if (!b->buf)
		return -ENOMEM;
	b->len = 0;
	b->cap = initial;
	return 0;
}

static void kbuf_free(struct kbuf *b)
{
	kfree(b->buf);
	b->buf = NULL;
	b->len = b->cap = 0;
}

static int kbuf_reserve(struct kbuf *b, size_t n)
{
	if (b->len + n <= b->cap)
		return 0;
	{
		size_t ncap = b->cap;
		void *nb;

		while (b->len + n > ncap)
			ncap *= 2;
		nb = krealloc(b->buf, ncap, GFP_KERNEL);
		if (!nb)
			return -ENOMEM;
		b->buf = nb;
		b->cap = ncap;
	}
	return 0;
}

static inline int EMIT(struct kbuf *b, u8 byte)
{
	if (kbuf_reserve(b, 1))
		return -ENOMEM;
	b->buf[b->len++] = byte;
	return 0;
}

static int emit_u32le(struct kbuf *b, u32 v)
{
	if (kbuf_reserve(b, 4))
		return -ENOMEM;
	b->buf[b->len++] = (u8)(v);
	b->buf[b->len++] = (u8)(v >> 8);
	b->buf[b->len++] = (u8)(v >> 16);
	b->buf[b->len++] = (u8)(v >> 24);
	return 0;
}

static int emit_u64le(struct kbuf *b, u64 v)
{
	if (kbuf_reserve(b, 8))
		return -ENOMEM;
	for (int i = 0; i < 8; i++)
		b->buf[b->len++] = (u8)(v >> (i * 8));
	return 0;
}

/* -----------------------------------------------------------------------
 * x86_64 instruction emitters
 * (identical encoding to the userland emit.c; see that file for comments)
 * ----------------------------------------------------------------------- */
#define REGS_BYTES  256          /* 32 * 8 bytes                          */
#define DISP32(rv)  ((s32)((rv) * 8))

static int emit_push_rbx(struct kbuf *b) { return EMIT(b, 0x53); }
static int emit_pop_rbx(struct kbuf *b)  { return EMIT(b, 0x5b); }
static int emit_ret(struct kbuf *b)      { return EMIT(b, 0xc3); }

static int emit_sub_rsp_imm32(struct kbuf *b, s32 imm)
{
	/* 48 81 EC imm32 */
	return EMIT(b, 0x48) || EMIT(b, 0x81) || EMIT(b, 0xec) ||
	       emit_u32le(b, (u32)imm);
}

static int emit_add_rsp_imm32(struct kbuf *b, s32 imm)
{
	/* 48 81 C4 imm32 */
	return EMIT(b, 0x48) || EMIT(b, 0x81) || EMIT(b, 0xc4) ||
	       emit_u32le(b, (u32)imm);
}

/* mov rbx, rsp */
static int emit_mov_rbx_rsp(struct kbuf *b)
{
	/* 48 89 E3 */
	return EMIT(b, 0x48) || EMIT(b, 0x89) || EMIT(b, 0xe3);
}

/* mov [rbx + disp32], rax */
static int emit_store_rax(struct kbuf *b, s32 disp)
{
	/* 48 89 83 disp32 */
	return EMIT(b, 0x48) || EMIT(b, 0x89) || EMIT(b, 0x83) ||
	       emit_u32le(b, (u32)disp);
}

/* mov rax, [rbx + disp32] */
static int emit_load_rax(struct kbuf *b, s32 disp)
{
	/* 48 8B 83 disp32 */
	return EMIT(b, 0x48) || EMIT(b, 0x8b) || EMIT(b, 0x83) ||
	       emit_u32le(b, (u32)disp);
}

/* mov rcx, [rbx + disp32] */
static int emit_load_rcx(struct kbuf *b, s32 disp)
{
	/* 48 8B 8B disp32 */
	return EMIT(b, 0x48) || EMIT(b, 0x8b) || EMIT(b, 0x8b) ||
	       emit_u32le(b, (u32)disp);
}

/* mov [rbx + disp32], rdi */
static int emit_store_rdi(struct kbuf *b, s32 disp)
{
	/* 48 89 BB disp32 */
	return EMIT(b, 0x48) || EMIT(b, 0x89) || EMIT(b, 0xbb) ||
	       emit_u32le(b, (u32)disp);
}

/* xor rax, rax */
static int emit_xor_rax_rax(struct kbuf *b)
{
	/* 48 31 C0 */
	return EMIT(b, 0x48) || EMIT(b, 0x31) || EMIT(b, 0xc0);
}

/* mov rax, imm64 */
static int emit_mov_rax_imm64(struct kbuf *b, u64 v)
{
	/* 48 B8 imm64 */
	return EMIT(b, 0x48) || EMIT(b, 0xb8) || emit_u64le(b, v);
}

/* add rax, imm32 */
static int emit_add_rax_imm32(struct kbuf *b, s32 imm)
{
	/* 48 05 imm32 */
	return EMIT(b, 0x48) || EMIT(b, 0x05) || emit_u32le(b, (u32)imm);
}

/* add rax, rcx */
static int emit_add_rax_rcx(struct kbuf *b)
{
	/* 48 01 C8 */
	return EMIT(b, 0x48) || EMIT(b, 0x01) || EMIT(b, 0xc8);
}

/* sub rax, rcx */
static int emit_sub_rax_rcx(struct kbuf *b)
{
	/* 48 29 C8 */
	return EMIT(b, 0x48) || EMIT(b, 0x29) || EMIT(b, 0xc8);
}

/* or rax, rcx */
static int emit_or_rax_rcx(struct kbuf *b)
{
	/* 48 09 C8 */
	return EMIT(b, 0x48) || EMIT(b, 0x09) || EMIT(b, 0xc8);
}

/* and rax, rcx */
static int emit_and_rax_rcx(struct kbuf *b)
{
	/* 48 21 C8 */
	return EMIT(b, 0x48) || EMIT(b, 0x21) || EMIT(b, 0xc8);
}

/* xor rax, rcx */
static int emit_xor_rax_rcx(struct kbuf *b)
{
	/* 48 31 C8 */
	return EMIT(b, 0x48) || EMIT(b, 0x31) || EMIT(b, 0xc8);
}

/* imul rax, rcx */
static int emit_imul_rax_rcx(struct kbuf *b)
{
	/* 48 0F AF C1 */
	return EMIT(b, 0x48) || EMIT(b, 0x0f) || EMIT(b, 0xaf) || EMIT(b, 0xc1);
}

/* shl rax, imm8 */
static int emit_shl_rax_imm8(struct kbuf *b, u8 imm)
{
	/* 48 C1 E0 imm8 */
	return EMIT(b, 0x48) || EMIT(b, 0xc1) || EMIT(b, 0xe0) || EMIT(b, imm);
}

/* shr rax, imm8 */
static int emit_shr_rax_imm8(struct kbuf *b, u8 imm)
{
	/* 48 C1 E8 imm8 */
	return EMIT(b, 0x48) || EMIT(b, 0xc1) || EMIT(b, 0xe8) || EMIT(b, imm);
}

/* sar rax, imm8 */
static int emit_sar_rax_imm8(struct kbuf *b, u8 imm)
{
	/* 48 C1 F8 imm8 */
	return EMIT(b, 0x48) || EMIT(b, 0xc1) || EMIT(b, 0xf8) || EMIT(b, imm);
}

/* mov rdi, rbx */
static int emit_mov_rdi_rbx(struct kbuf *b)
{
	/* 48 89 DF */
	return EMIT(b, 0x48) || EMIT(b, 0x89) || EMIT(b, 0xdf);
}

/* call rax */
static int emit_call_rax(struct kbuf *b)
{
	/* FF D0 */
	return EMIT(b, 0xff) || EMIT(b, 0xd0);
}

/* -----------------------------------------------------------------------
 * Composed sequences
 * ----------------------------------------------------------------------- */
static int emit_load_rs_pair(struct kbuf *b, u8 rs1, u8 rs2)
{
	return emit_load_rax(b, DISP32(rs1)) || emit_load_rcx(b, DISP32(rs2));
}

static int emit_store_rd(struct kbuf *b, u8 rd)
{
	if (rd == 0)
		return 0;  /* x0 is always zero; writes are discarded */
	return emit_store_rax(b, DISP32(rd));
}

static int emit_prologue(struct kbuf *b)
{
	int r;

	if (emit_push_rbx(b))                         return -1;
	if (emit_sub_rsp_imm32(b, REGS_BYTES))         return -1;
	if (emit_mov_rbx_rsp(b))                       return -1;
	/* Zero all regs[] */
	if (emit_xor_rax_rax(b))                       return -1;
	for (r = 0; r < 32; r++)
		if (emit_store_rax(b, DISP32(r)))      return -1;
	/* regs[10] (a0) = ctx (rdi) */
	return emit_store_rdi(b, DISP32(10));
}

static int emit_epilogue(struct kbuf *b)
{
	if (emit_load_rax(b, DISP32(10)))  return -1;
	if (emit_add_rsp_imm32(b, REGS_BYTES)) return -1;
	if (emit_pop_rbx(b))               return -1;
	return emit_ret(b);
}

/* -----------------------------------------------------------------------
 * Per-instruction translation
 * ----------------------------------------------------------------------- */
static int translate_one(struct kbuf *b, const struct merlin_insn *in,
			 u64 tramp_addr)
{
	switch (in->cls) {

	case INSN_LUI:
		if (emit_mov_rax_imm64(b, (u64)in->imm))  return -1;
		return emit_store_rd(b, in->rd);

	case INSN_ALU_IMM:
		if (emit_load_rax(b, DISP32(in->rs1)))    return -1;
		switch (in->alu_op) {
		case ALU_ADD:
			if (emit_add_rax_imm32(b, (s32)in->imm)) return -1;
			break;
		default:
			/* Unsupported ALU-imm variant: emit a 0 for now and log */
			pr_debug("merlin/jit-x86: alu-imm op %d not yet supported "
				 "(pc=%u)\n", in->alu_op, in->pc);
			if (emit_xor_rax_rax(b)) return -1;
			break;
		}
		return emit_store_rd(b, in->rd);

	case INSN_ALU_REG:
		if (emit_load_rs_pair(b, in->rs1, in->rs2)) return -1;
		switch (in->alu_op) {
		case ALU_ADD:  if (emit_add_rax_rcx(b))  return -1; break;
		case ALU_SUB:  if (emit_sub_rax_rcx(b))  return -1; break;
		case ALU_OR:   if (emit_or_rax_rcx(b))   return -1; break;
		case ALU_AND:  if (emit_and_rax_rcx(b))  return -1; break;
		case ALU_XOR:  if (emit_xor_rax_rcx(b))  return -1; break;
		default:
			pr_debug("merlin/jit-x86: alu-reg op %d not yet supported "
				 "(pc=%u)\n", in->alu_op, in->pc);
			if (emit_xor_rax_rax(b)) return -1;
			break;
		}
		return emit_store_rd(b, in->rd);

	case INSN_MUL:
		if (in->alu_op != ALU_MUL) {
			pr_debug("merlin/jit-x86: mul variant %d unsupported\n",
				 in->alu_op);
			if (emit_xor_rax_rax(b)) return -1;
			return emit_store_rd(b, in->rd);
		}
		if (emit_load_rs_pair(b, in->rs1, in->rs2)) return -1;
		if (emit_imul_rax_rcx(b))                   return -1;
		return emit_store_rd(b, in->rd);

	case INSN_SHIFT_IMM:
		if (emit_load_rax(b, DISP32(in->rs1)))      return -1;
		switch (in->alu_op) {
		case ALU_SLL: if (emit_shl_rax_imm8(b, (u8)in->imm)) return -1; break;
		case ALU_SRL: if (emit_shr_rax_imm8(b, (u8)in->imm)) return -1; break;
		case ALU_SRA: if (emit_sar_rax_imm8(b, (u8)in->imm)) return -1; break;
		default: return -1;
		}
		return emit_store_rd(b, in->rd);

	case INSN_ECALL:
		/* Pass &regs[0] to the trampoline */
		if (emit_mov_rdi_rbx(b))                   return -1;
		if (emit_mov_rax_imm64(b, tramp_addr))     return -1;
		return emit_call_rax(b);

	case INSN_JALR:
		/* `jalr x0, ra, 0` is the return pattern; emit epilogue. */
		return emit_epilogue(b);

	case INSN_FENCE:
		/* No-op: x86_64 TSO is stronger than RVWMO. */
		return 0;

	case INSN_BRANCH:
	case INSN_JAL:
	case INSN_LOAD:
	case INSN_STORE:
		/* TODO(phase2): full branch / memory translation */
		pr_debug("merlin/jit-x86: %s at pc=%u not yet fully translated; "
			 "emitting no-op\n", merlin_insn_class_name(in->cls), in->pc);
		return 0;

	default:
		pr_debug("merlin/jit-x86: class %s unsupported at pc=%u\n",
			 merlin_insn_class_name(in->cls), in->pc);
		return -ENOSYS;
	}
}

/* -----------------------------------------------------------------------
 * Trampoline
 *
 * Called from JIT'd ecall site with rdi = &regs[0].
 * Reads a7/a0-a5, dispatches, writes a0.
 * Declared in a separate asm stub; for the kernel we define it in C using
 * a packed regs pointer.
 * ----------------------------------------------------------------------- */
void merlin_jit_x86_helper_trampoline(u64 *regs)
{
	u32 helper_id = (u32)regs[17];   /* a7 */
	u64 ret;

	ret = merlin_dispatch_helper(helper_id,
				     regs[10], regs[11], regs[12],
				     regs[13], regs[14], regs[15]);
	regs[10] = ret;  /* a0 = return value */
}

/* -----------------------------------------------------------------------
 * merlin_jit_x86_translate — top-level translate entry
 * ----------------------------------------------------------------------- */
static int x86_translate(const u8 *text, size_t text_len,
			 struct merlin_prog *prog,
			 struct merlin_jit_image **img_out)
{
	struct kbuf b;
	struct merlin_jit_image *img;
	void *exec;
	size_t exec_pages;
	size_t code_len;
	u64 tramp_addr;
	u32 pc;
	bool saw_ret = false;
	int rc;

	if (!text_len || text_len % 4)
		return -EINVAL;

	rc = kbuf_init(&b, 4096);
	if (rc)
		return rc;

	tramp_addr = (u64)(uintptr_t)merlin_jit_x86_helper_trampoline;

	if (emit_prologue(&b)) {
		kbuf_free(&b);
		return -ENOMEM;
	}

	for (pc = 0; pc < text_len; pc += 4) {
		struct merlin_insn in;
		u32 w;

		w  = (u32)text[pc];
		w |= (u32)text[pc + 1] << 8;
		w |= (u32)text[pc + 2] << 16;
		w |= (u32)text[pc + 3] << 24;

		if (merlin_decode(w, pc, &in) < 0) {
			pr_debug("merlin/jit-x86: decode failure at pc=%u\n", pc);
			kbuf_free(&b);
			return -EINVAL;
		}

		rc = translate_one(&b, &in, tramp_addr);
		if (rc) {
			kbuf_free(&b);
			return rc;
		}

		if (in.cls == INSN_JALR) {
			saw_ret = true;
			break;
		}
	}

	if (!saw_ret) {
		if (emit_epilogue(&b)) {
			kbuf_free(&b);
			return -ENOMEM;
		}
	}

	/* Allocate executable memory via vmalloc */
	code_len   = b.len;
	exec_pages = DIV_ROUND_UP(code_len, PAGE_SIZE);
	exec = vmalloc_exec(exec_pages * PAGE_SIZE);
	if (!exec) {
		kbuf_free(&b);
		return -ENOMEM;
	}

	memcpy(exec, b.buf, code_len);
	kbuf_free(&b);

#ifdef CONFIG_X86_64
	/* Flush instruction cache */
	flush_icache_range((unsigned long)exec,
			   (unsigned long)exec + exec_pages * PAGE_SIZE);
#endif

	img = kzalloc(sizeof(*img), GFP_KERNEL);
	if (!img) {
		vfree(exec);
		return -ENOMEM;
	}

	img->image     = exec;
	img->image_len = exec_pages * PAGE_SIZE;
	img->code_len  = code_len;

	*img_out = img;
	pr_debug("merlin: x86_64 JIT translated %zu RV insns → %zu x86 bytes\n",
		 (size_t)(pc / 4), code_len);
	return 0;
}

static void x86_free_image(struct merlin_jit_image *img)
{
	if (img) {
		vfree(img->image);
		kfree(img);
	}
}

const struct merlin_jit_ops merlin_jit_x86_64 = {
	.name        = "x86_64",
	.translate   = x86_translate,
	.free_image  = x86_free_image,
};
EXPORT_SYMBOL_GPL(merlin_jit_x86_64);
