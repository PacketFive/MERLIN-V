// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * jit.c - MERLIN-V (RV64) -> x86_64 single-pass translator.
 *
 * Spill-everything register strategy.  RV regs x0..x31 live in a
 * 32-slot uint64_t array (regs[]) on the JIT'd function's stack
 * frame; rbx points at &regs[0] for the duration of the function.
 *
 * Prologue:
 *
 *   push  rbx                  ; callee-saved we're using
 *   sub   rsp, 256              ; 32 * 8 byte regs[]
 *   mov   rbx, rsp              ; rbx = &regs[0]
 *   xor   rax, rax
 *   ; zero regs[0..31] - 32 stores
 *   mov   [rbx + 80], rdi       ; regs[10] = ctx  (a0 = arg0)
 *
 * Epilogue (emitted on jalr ra,x0):
 *
 *   mov   rax, [rbx + 80]       ; return value = a0
 *   add   rsp, 256
 *   pop   rbx
 *   ret
 *
 * Per-RV-instruction translation pattern (the generic shape):
 *
 *   mov   rax, [rbx + rs1*8]
 *   mov   rcx, [rbx + rs2*8]
 *   <op>  rax, rcx
 *   mov   [rbx + rd*8], rax
 *
 * ALU-with-immediate skips the rcx load and uses an x86 imm32 form.
 *
 * ecall translates to:
 *
 *   mov   rdi, rbx                  ; arg0 = &regs[0]
 *   mov   rax, <trampoline ptr>
 *   call  rax
 *
 * The trampoline reads a7 / a0..a5 from regs[] and writes a0 back.
 */
#include "jit.h"
#include "emit.h"
#include "decode.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

/* Conveniences */
#define REGS_BYTES        256                  /* 32 * 8 */
#define DISP_OF(rv_reg)   ((int32_t)((rv_reg) * 8))

#define JFAIL(out, fmt, ...) do {                                       \
	snprintf((out)->error, sizeof((out)->error), fmt, ##__VA_ARGS__);\
	(out)->ok = false;                                              \
	return -1;                                                      \
} while (0)

#define JFAIL_FREE(out, fmt, ...) do {                                  \
	snprintf((out)->error, sizeof((out)->error), fmt, ##__VA_ARGS__);\
	(out)->ok = false;                                              \
	emit_buf_free(&buf);                                            \
	return -1;                                                      \
} while (0)

static int emit_prologue(struct emit_buf *buf, uint64_t tramp_unused)
{
	(void)tramp_unused;
	if (emit_push_r64(buf, X86_RBX))                  return -1;
	if (emit_sub_rsp_imm32(buf, REGS_BYTES))          return -1;
	if (emit_mov_rbx_rsp(buf))                        return -1;
	/* Zero regs[] -- a quick loop unrolled to 32 stores of rax=0. */
	if (emit_xor_rax_rax(buf))                        return -1;
	for (int i = 0; i < 32; i++) {
		if (emit_mov_mem_disp_from_rax(buf, DISP_OF(i)))
			return -1;
	}
	/* a0 (regs[10]) = ctx (rdi) */
	if (emit_mov_mem_disp_from_rdi(buf, DISP_OF(10))) return -1;
	return 0;
}

static int emit_epilogue(struct emit_buf *buf)
{
	if (emit_mov_rax_from_mem_disp(buf, DISP_OF(10)))  return -1;
	if (emit_add_rsp_imm32(buf, REGS_BYTES))           return -1;
	if (emit_pop_r64(buf, X86_RBX))                    return -1;
	return emit_ret(buf);
}

static int emit_load_rs_pair(struct emit_buf *buf, int rs1, int rs2)
{
	if (emit_mov_rax_from_mem_disp(buf, DISP_OF(rs1))) return -1;
	if (emit_mov_rcx_from_mem_disp(buf, DISP_OF(rs2))) return -1;
	return 0;
}

static int emit_store_rax_to_rd(struct emit_buf *buf, int rd)
{
	if (rd == 0)
		return 0;          /* x0 hard-wired to zero; no-op */
	return emit_mov_mem_disp_from_rax(buf, DISP_OF(rd));
}

/* Helper-call site: invoke the trampoline pointer with rdi=&regs[]. */
static int emit_helper_call(struct emit_buf *buf, uint64_t tramp_addr)
{
	if (emit_mov_rdi_rbx(buf))                    return -1;
	if (emit_mov_rax_imm64(buf, tramp_addr))      return -1;
	return emit_call_rax(buf);
}

/* Translate one RV instruction.  Returns 0 on success; on failure
 * fills out->error and returns -1.
 */
static int translate_one(struct emit_buf *buf,
			 const struct merlin_insn *in,
			 uint64_t tramp_addr,
			 struct merlin_jit_result *out)
{
	switch (in->cls) {
	case INSN_ALU_IMM: {
		/* rax = regs[rs1]; rax <op>= imm; regs[rd] = rax */
		if (emit_mov_rax_from_mem_disp(buf, DISP_OF(in->rs1)))
			JFAIL(out, "addi load fail");
		switch (in->alu_op) {
		case ALU_ADD:
			if (emit_add_rax_imm32(buf, (int32_t)in->imm))
				JFAIL(out, "addi imm fail");
			break;
		default:
			JFAIL(out,
			      "alu-imm op %d not yet supported (PC=0x%x)",
			      in->alu_op, in->pc);
		}
		return emit_store_rax_to_rd(buf, in->rd);
	}

	case INSN_LUI: {
		/* rax = imm (already shifted in immU); regs[rd] = rax */
		if (emit_mov_rax_imm64(buf, (uint64_t)(int64_t)in->imm))
			JFAIL(out, "lui imm fail");
		return emit_store_rax_to_rd(buf, in->rd);
	}

	case INSN_ALU_REG: {
		if (emit_load_rs_pair(buf, in->rs1, in->rs2))
			JFAIL(out, "alu load fail");
		switch (in->alu_op) {
		case ALU_ADD: if (emit_add_rax_rcx(buf))  JFAIL(out, "add fail"); break;
		case ALU_SUB: if (emit_sub_rax_rcx(buf))  JFAIL(out, "sub fail"); break;
		case ALU_OR:  if (emit_or_rax_rcx (buf))  JFAIL(out, "or fail");  break;
		case ALU_AND: if (emit_and_rax_rcx(buf))  JFAIL(out, "and fail"); break;
		case ALU_XOR: if (emit_xor_rax_rcx(buf))  JFAIL(out, "xor fail"); break;
		default:
			JFAIL(out,
			      "alu-reg op %d not yet supported (PC=0x%x)",
			      in->alu_op, in->pc);
		}
		return emit_store_rax_to_rd(buf, in->rd);
	}

	case INSN_MUL: {
		if (in->alu_op != ALU_MUL)
			JFAIL(out, "MUL variant %d unsupported", in->alu_op);
		if (emit_load_rs_pair(buf, in->rs1, in->rs2))
			JFAIL(out, "mul load fail");
		if (emit_imul_rax_rcx(buf))
			JFAIL(out, "imul fail");
		return emit_store_rax_to_rd(buf, in->rd);
	}

	case INSN_SHIFT_IMM: {
		if (emit_mov_rax_from_mem_disp(buf, DISP_OF(in->rs1)))
			JFAIL(out, "shamt load fail");
		switch (in->alu_op) {
		case ALU_SLL: if (emit_shl_rax_imm8(buf, (uint8_t)in->imm)) JFAIL(out, "slli fail"); break;
		case ALU_SRL: if (emit_shr_rax_imm8(buf, (uint8_t)in->imm)) JFAIL(out, "srli fail"); break;
		case ALU_SRA: if (emit_sar_rax_imm8(buf, (uint8_t)in->imm)) JFAIL(out, "srai fail"); break;
		default: JFAIL(out, "shift variant %d unsupported", in->alu_op);
		}
		return emit_store_rax_to_rd(buf, in->rd);
	}

	case INSN_ECALL:
		if (emit_helper_call(buf, tramp_addr))
			JFAIL(out, "ecall trampoline emit fail");
		return 0;

	case INSN_JALR:
		/* `jalr x0, ra, 0` is the return-from-function pattern.
		 * The verifier has guaranteed this is the only jalr the
		 * program emits. We translate it to the epilogue. */
		return emit_epilogue(buf);

	case INSN_FENCE:
		/* fence: x86 is much stronger than RVWMO; this is a no-op
		 * at the host-JIT level (the verifier ensured the program
		 * is consistent with RVWMO, which x86_64 strictly
		 * dominates for the atomics MERLIN-V supports today). */
		return 0;

	default:
		JFAIL(out,
		      "instruction class '%s' at PC=0x%x not yet supported "
		      "by this prototype JIT",
		      merlin_insn_class_name(in->cls), in->pc);
	}
}

int merlin_jit_translate(const uint8_t *text, size_t text_size,
			 merlin_helper_trampoline tramp,
			 struct merlin_jit_result *out)
{
	struct emit_buf buf;

	memset(out, 0, sizeof(*out));

	if (text_size == 0 || text_size % 4) {
		snprintf(out->error, sizeof(out->error),
			 ".text size %zu invalid", text_size);
		out->ok = false;
		return -1;
	}
	if (emit_buf_init(&buf, 4096) < 0) {
		snprintf(out->error, sizeof(out->error), "oom");
		out->ok = false;
		return -1;
	}

	uint64_t tramp_addr = (uint64_t)(uintptr_t)tramp;

	if (emit_prologue(&buf, tramp_addr) < 0)
		JFAIL_FREE(out, "prologue emit failed");

	bool saw_ret = false;
	for (uint32_t pc = 0; pc < text_size; pc += 4) {
		uint32_t w =  (uint32_t)text[pc]
			   | ((uint32_t)text[pc + 1] <<  8)
			   | ((uint32_t)text[pc + 2] << 16)
			   | ((uint32_t)text[pc + 3] << 24);

		struct merlin_insn in;
		if (merlin_decode(w, pc, &in) < 0)
			JFAIL_FREE(out, "decode failure at PC=0x%x", pc);

		if (translate_one(&buf, &in, tramp_addr, out) < 0) {
			emit_buf_free(&buf);
			return -1;
		}

		if (in.cls == INSN_JALR) {
			saw_ret = true;
			break;
		}
	}

	if (!saw_ret) {
		/* Fall-through return: emit epilogue so the JIT'd code
		 * always returns. */
		if (emit_epilogue(&buf) < 0)
			JFAIL_FREE(out, "fallthrough epilogue emit failed");
	}

	/* mmap executable */
	long page = sysconf(_SC_PAGESIZE);
	size_t code_len = buf.len;
	size_t alloc = ((code_len + page - 1) / page) * page;
	void *p = mmap(NULL, alloc,
		       PROT_READ | PROT_WRITE | PROT_EXEC,
		       MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (p == MAP_FAILED)
		JFAIL_FREE(out, "mmap failed");

	memcpy(p, buf.buf, code_len);
	emit_buf_free(&buf);

	if (mprotect(p, alloc, PROT_READ | PROT_EXEC) < 0) {
		munmap(p, alloc);
		snprintf(out->error, sizeof(out->error), "mprotect failed");
		out->ok = false;
		return -1;
	}

	out->exec        = p;
	out->exec_len    = alloc;
	out->code_bytes  = code_len;
	out->fn          = (merlin_jit_fn)p;
	out->ok          = true;
	out->error[0]    = '\0';
	return 0;
}

void merlin_jit_free(struct merlin_jit_result *out)
{
	if (out->exec)
		munmap(out->exec, out->exec_len);
	memset(out, 0, sizeof(*out));
}
