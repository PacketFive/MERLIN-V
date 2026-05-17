/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * emit.h - low-level x86_64 instruction emitter for the MERLIN-V JIT.
 *
 * Each emit_* function appends a known-good byte sequence to the
 * code buffer.  No assembler dependency, no template fixup - just
 * raw bytes.  The set is deliberately small: we emit exactly the
 * x86_64 patterns the RV->x86 translator needs and no others.
 *
 * Register-number convention (matches Intel manual encoding):
 *
 *   x86  reg  =  0 rax, 1 rcx, 2 rdx, 3 rbx, 4 rsp, 5 rbp,
 *                6 rsi, 7 rdi, 8 r8 .. 15 r15
 *
 * Calling convention in this prototype:
 *
 *   - The JIT'd function signature is:
 *         uint64_t prog(void *ctx);
 *     SysV ABI -> ctx is in rdi at entry, return value in rax.
 *
 *   - On entry, the prologue saves rbx (callee-saved), reserves 256
 *     bytes of stack for the regs[] array, points rbx at regs[0],
 *     zeroes regs[0..31], then writes ctx into regs[10] (a0).
 *
 *   - All RV register accesses go through "[rbx + N*8]" -- regs[]
 *     lives on the JIT'd function's own stack frame.
 *
 *   - rax/rcx/rdx are the scratch x86 registers the emitter uses
 *     internally; no other x86 register holds anything across an
 *     instruction boundary.
 */
#ifndef MERLIN_JIT_EMIT_H
#define MERLIN_JIT_EMIT_H

#include <stddef.h>
#include <stdint.h>

struct emit_buf {
	uint8_t *buf;
	size_t   len;
	size_t   cap;
};

/* x86_64 register IDs */
enum {
	X86_RAX = 0, X86_RCX = 1, X86_RDX = 2, X86_RBX = 3,
	X86_RSP = 4, X86_RBP = 5, X86_RSI = 6, X86_RDI = 7,
	X86_R8  = 8, X86_R9  = 9, X86_R10 = 10, X86_R11 = 11,
	X86_R12 = 12, X86_R13 = 13, X86_R14 = 14, X86_R15 = 15,
};

int  emit_buf_init(struct emit_buf *e, size_t cap);
void emit_buf_free(struct emit_buf *e);
int  emit_byte(struct emit_buf *e, uint8_t b);
int  emit_u32(struct emit_buf *e, uint32_t v);
int  emit_u64(struct emit_buf *e, uint64_t v);
int  emit_bytes(struct emit_buf *e, const uint8_t *p, size_t n);

/* ---- the small set of x86_64 operations we use ---- */

/* push/pop r64 (only used for rbx in prologue/epilogue) */
int emit_push_r64(struct emit_buf *e, int r);
int emit_pop_r64 (struct emit_buf *e, int r);

/* sub/add rsp, imm32 */
int emit_sub_rsp_imm32(struct emit_buf *e, int32_t imm);
int emit_add_rsp_imm32(struct emit_buf *e, int32_t imm);

/* mov rbx, rsp     (used in prologue to anchor regs[]) */
int emit_mov_rbx_rsp(struct emit_buf *e);

/* mov [rbx + disp32], rdi   (store ctx into regs[10]) */
int emit_mov_mem_disp_from_rdi(struct emit_buf *e, int32_t disp);

/* mov rax, [rbx + disp32]   /  mov [rbx + disp32], rax */
int emit_mov_rax_from_mem_disp(struct emit_buf *e, int32_t disp);
int emit_mov_mem_disp_from_rax(struct emit_buf *e, int32_t disp);

/* mov rcx, [rbx + disp32]   /  mov [rbx + disp32], rcx */
int emit_mov_rcx_from_mem_disp(struct emit_buf *e, int32_t disp);

/* mov rdi, rbx   (pass &regs[0] to a helper-trampoline call) */
int emit_mov_rdi_rbx(struct emit_buf *e);

/* mov rax, imm64 / mov r10, imm64
 * Use mov r64, imm64 (REX.W=1, opcode=B8+rd, imm64).
 */
int emit_mov_rax_imm64(struct emit_buf *e, uint64_t imm);
int emit_mov_r10_imm64(struct emit_buf *e, uint64_t imm);

/* xor rax, rax  (zero rax) */
int emit_xor_rax_rax(struct emit_buf *e);

/* ALU on rax:
 *   add rax, rcx
 *   sub rax, rcx
 *   or  rax, rcx
 *   and rax, rcx
 *   xor rax, rcx
 *   imul rax, rcx
 */
int emit_add_rax_rcx(struct emit_buf *e);
int emit_sub_rax_rcx(struct emit_buf *e);
int emit_or_rax_rcx (struct emit_buf *e);
int emit_and_rax_rcx(struct emit_buf *e);
int emit_xor_rax_rcx(struct emit_buf *e);
int emit_imul_rax_rcx(struct emit_buf *e);

/* add rax, imm32 (sign-extended) */
int emit_add_rax_imm32(struct emit_buf *e, int32_t imm);

/* shifts: shl/shr/sar rax, imm8 */
int emit_shl_rax_imm8(struct emit_buf *e, uint8_t imm);
int emit_shr_rax_imm8(struct emit_buf *e, uint8_t imm);
int emit_sar_rax_imm8(struct emit_buf *e, uint8_t imm);

/* call rax  (indirect call via rax; we materialise the trampoline
 * pointer with mov rax, imm64 first) */
int emit_call_rax(struct emit_buf *e);

/* ret */
int emit_ret(struct emit_buf *e);

#endif /* MERLIN_JIT_EMIT_H */
