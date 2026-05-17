// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * emit.c - x86_64 byte-level emitter.
 *
 * Hand-encoded sequences.  Every emit_* function corresponds to
 * exactly one x86_64 instruction; bytes are written little-endian
 * where multi-byte fields appear (Intel manual encoding).
 *
 * Reference: Intel SDM Vol 2 (instruction set reference).
 *
 * Notes:
 *
 *  - REX prefix layout: 0x48 = REX.W = 1 (64-bit operand size).
 *    Additional bits set REX.R / REX.X / REX.B for register-number
 *    bit 3.  We use 0x49 when we need REX.B set (high-numbered
 *    registers like r10).
 *
 *  - ModR/M byte: mod[7..6] reg[5..3] rm[2..0].  When we need a
 *    [base + disp32] addressing form: mod=10, rm=base reg.
 *    For [rbx + disp32]: mod=10 reg=<dst> rm=011 -> base byte 0x83
 *    plus reg-field bits.
 */

#include "emit.h"

#include <stdlib.h>
#include <string.h>

int emit_buf_init(struct emit_buf *e, size_t cap)
{
	e->buf = malloc(cap);
	if (!e->buf)
		return -1;
	e->len = 0;
	e->cap = cap;
	return 0;
}

void emit_buf_free(struct emit_buf *e)
{
	free(e->buf);
	e->buf = NULL;
	e->len = e->cap = 0;
}

static int reserve(struct emit_buf *e, size_t n)
{
	if (e->len + n > e->cap) {
		size_t ncap = e->cap * 2;
		while (e->len + n > ncap)
			ncap *= 2;
		uint8_t *nb = realloc(e->buf, ncap);
		if (!nb)
			return -1;
		e->buf = nb;
		e->cap = ncap;
	}
	return 0;
}

int emit_byte(struct emit_buf *e, uint8_t b)
{
	if (reserve(e, 1)) return -1;
	e->buf[e->len++] = b;
	return 0;
}

int emit_u32(struct emit_buf *e, uint32_t v)
{
	if (reserve(e, 4)) return -1;
	e->buf[e->len++] = (uint8_t)(v      );
	e->buf[e->len++] = (uint8_t)(v >>  8);
	e->buf[e->len++] = (uint8_t)(v >> 16);
	e->buf[e->len++] = (uint8_t)(v >> 24);
	return 0;
}

int emit_u64(struct emit_buf *e, uint64_t v)
{
	if (reserve(e, 8)) return -1;
	for (int i = 0; i < 8; i++)
		e->buf[e->len++] = (uint8_t)((v >> (i * 8)) & 0xff);
	return 0;
}

int emit_bytes(struct emit_buf *e, const uint8_t *p, size_t n)
{
	if (reserve(e, n)) return -1;
	memcpy(e->buf + e->len, p, n);
	e->len += n;
	return 0;
}

/* -------- push/pop r64 (low-numbered regs only) -------- */
int emit_push_r64(struct emit_buf *e, int r)
{
	/* PUSH r64: 0x50 + r (for r < 8) */
	return emit_byte(e, (uint8_t)(0x50 + (r & 7)));
}

int emit_pop_r64(struct emit_buf *e, int r)
{
	/* POP r64: 0x58 + r */
	return emit_byte(e, (uint8_t)(0x58 + (r & 7)));
}

/* -------- stack adjustments -------- */
int emit_sub_rsp_imm32(struct emit_buf *e, int32_t imm)
{
	/* REX.W 81 /5 imm32 -> sub rsp, imm32
	 * 48 81 EC <imm32>
	 */
	if (emit_byte(e, 0x48)) return -1;
	if (emit_byte(e, 0x81)) return -1;
	if (emit_byte(e, 0xEC)) return -1;
	return emit_u32(e, (uint32_t)imm);
}

int emit_add_rsp_imm32(struct emit_buf *e, int32_t imm)
{
	/* 48 81 C4 <imm32> */
	if (emit_byte(e, 0x48)) return -1;
	if (emit_byte(e, 0x81)) return -1;
	if (emit_byte(e, 0xC4)) return -1;
	return emit_u32(e, (uint32_t)imm);
}

/* -------- mov rbx, rsp -------- */
int emit_mov_rbx_rsp(struct emit_buf *e)
{
	/* REX.W 89 D=rsp/M=rbx -> 48 89 E3 */
	static const uint8_t seq[] = { 0x48, 0x89, 0xE3 };
	return emit_bytes(e, seq, sizeof(seq));
}

/* -------- mov [rbx + disp32], rdi -------- */
int emit_mov_mem_disp_from_rdi(struct emit_buf *e, int32_t disp)
{
	/* 48 89 BB disp32 */
	if (emit_byte(e, 0x48)) return -1;
	if (emit_byte(e, 0x89)) return -1;
	if (emit_byte(e, 0xBB)) return -1;
	return emit_u32(e, (uint32_t)disp);
}

/* -------- mov rax, [rbx + disp32] -------- */
int emit_mov_rax_from_mem_disp(struct emit_buf *e, int32_t disp)
{
	/* 48 8B 83 disp32 */
	if (emit_byte(e, 0x48)) return -1;
	if (emit_byte(e, 0x8B)) return -1;
	if (emit_byte(e, 0x83)) return -1;
	return emit_u32(e, (uint32_t)disp);
}

/* -------- mov [rbx + disp32], rax -------- */
int emit_mov_mem_disp_from_rax(struct emit_buf *e, int32_t disp)
{
	/* 48 89 83 disp32 */
	if (emit_byte(e, 0x48)) return -1;
	if (emit_byte(e, 0x89)) return -1;
	if (emit_byte(e, 0x83)) return -1;
	return emit_u32(e, (uint32_t)disp);
}

/* -------- mov rcx, [rbx + disp32] -------- */
int emit_mov_rcx_from_mem_disp(struct emit_buf *e, int32_t disp)
{
	/* 48 8B 8B disp32 */
	if (emit_byte(e, 0x48)) return -1;
	if (emit_byte(e, 0x8B)) return -1;
	if (emit_byte(e, 0x8B)) return -1;
	return emit_u32(e, (uint32_t)disp);
}

/* -------- mov rdi, rbx -------- */
int emit_mov_rdi_rbx(struct emit_buf *e)
{
	/* 48 89 DF */
	static const uint8_t seq[] = { 0x48, 0x89, 0xDF };
	return emit_bytes(e, seq, sizeof(seq));
}

/* -------- mov r64, imm64 -------- */
int emit_mov_rax_imm64(struct emit_buf *e, uint64_t imm)
{
	/* 48 B8 imm64 */
	if (emit_byte(e, 0x48)) return -1;
	if (emit_byte(e, 0xB8)) return -1;
	return emit_u64(e, imm);
}

int emit_mov_r10_imm64(struct emit_buf *e, uint64_t imm)
{
	/* REX.WB = 0x49 (B set for r8-r15)
	 * opcode B8 + (r & 7) = BA for r10
	 */
	if (emit_byte(e, 0x49)) return -1;
	if (emit_byte(e, 0xBA)) return -1;
	return emit_u64(e, imm);
}

/* -------- xor rax, rax -------- */
int emit_xor_rax_rax(struct emit_buf *e)
{
	static const uint8_t seq[] = { 0x48, 0x31, 0xC0 };
	return emit_bytes(e, seq, sizeof(seq));
}

/* -------- rax <op>= rcx -------- */
int emit_add_rax_rcx(struct emit_buf *e)
{
	static const uint8_t seq[] = { 0x48, 0x01, 0xC8 };
	return emit_bytes(e, seq, sizeof(seq));
}
int emit_sub_rax_rcx(struct emit_buf *e)
{
	static const uint8_t seq[] = { 0x48, 0x29, 0xC8 };
	return emit_bytes(e, seq, sizeof(seq));
}
int emit_or_rax_rcx(struct emit_buf *e)
{
	static const uint8_t seq[] = { 0x48, 0x09, 0xC8 };
	return emit_bytes(e, seq, sizeof(seq));
}
int emit_and_rax_rcx(struct emit_buf *e)
{
	static const uint8_t seq[] = { 0x48, 0x21, 0xC8 };
	return emit_bytes(e, seq, sizeof(seq));
}
int emit_xor_rax_rcx(struct emit_buf *e)
{
	static const uint8_t seq[] = { 0x48, 0x31, 0xC8 };
	return emit_bytes(e, seq, sizeof(seq));
}
int emit_imul_rax_rcx(struct emit_buf *e)
{
	/* imul rax, rcx -> 48 0F AF C1 */
	static const uint8_t seq[] = { 0x48, 0x0F, 0xAF, 0xC1 };
	return emit_bytes(e, seq, sizeof(seq));
}

/* -------- add rax, imm32 (sign-extended) -------- */
int emit_add_rax_imm32(struct emit_buf *e, int32_t imm)
{
	/* 48 05 imm32 (ADD rax, imm32 sign-extended) */
	if (emit_byte(e, 0x48)) return -1;
	if (emit_byte(e, 0x05)) return -1;
	return emit_u32(e, (uint32_t)imm);
}

/* -------- shifts of rax by imm8 -------- */
int emit_shl_rax_imm8(struct emit_buf *e, uint8_t imm)
{
	/* 48 C1 E0 ib */
	if (emit_byte(e, 0x48)) return -1;
	if (emit_byte(e, 0xC1)) return -1;
	if (emit_byte(e, 0xE0)) return -1;
	return emit_byte(e, imm);
}
int emit_shr_rax_imm8(struct emit_buf *e, uint8_t imm)
{
	/* 48 C1 E8 ib */
	if (emit_byte(e, 0x48)) return -1;
	if (emit_byte(e, 0xC1)) return -1;
	if (emit_byte(e, 0xE8)) return -1;
	return emit_byte(e, imm);
}
int emit_sar_rax_imm8(struct emit_buf *e, uint8_t imm)
{
	/* 48 C1 F8 ib */
	if (emit_byte(e, 0x48)) return -1;
	if (emit_byte(e, 0xC1)) return -1;
	if (emit_byte(e, 0xF8)) return -1;
	return emit_byte(e, imm);
}

/* -------- call rax -------- */
int emit_call_rax(struct emit_buf *e)
{
	/* FF D0 */
	if (emit_byte(e, 0xFF)) return -1;
	return emit_byte(e, 0xD0);
}

/* -------- ret -------- */
int emit_ret(struct emit_buf *e)
{
	return emit_byte(e, 0xC3);
}
