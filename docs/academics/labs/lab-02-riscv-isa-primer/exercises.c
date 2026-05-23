// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * lab-02/exercises.c — RV32I encoding exercises.
 *
 * PARTIALLY PROVIDED — fill in the TODO slots.
 *
 * For each assembly snippet below, compute the 32-bit binary
 * encoding by hand from the RISC-V Unprivileged ISA manual and
 * substitute it for the `TODO_ENCODING` macro.
 *
 * Run `make test` after each change.  Autograder runs the same
 * tests plus a few additional ones the students don't see.
 */

#include "rv32.h"

#include <stdint.h>
#include <stdio.h>

#define TODO_ENCODING  0xDEADBEEFu

static int pass_count, total_count;

static void check(const char *name, uint32_t got, uint32_t want)
{
	total_count++;
	if (got == want) {
		fprintf(stderr, "  [PASS] %s\n", name);
		pass_count++;
	} else {
		fprintf(stderr, "  [FAIL] %s  expected 0x%08x got 0x%08x\n",
			name, want, got);
	}
}

int main(void)
{
	/* E1: addi a0, zero, 1   (I-type: imm=1, rs1=0, f3=0, rd=10, op=0x13) */
	check("E1 addi a0, zero, 1",       TODO_ENCODING, 0x00100513);

	/* E2: addi a0, zero, -1  (I-type: imm=-1, sign-extended 12-bit) */
	check("E2 addi a0, zero, -1",      TODO_ENCODING, 0xfff00513);

	/* E3: add a0, a0, a1     (R-type: f7=0, rs2=11, rs1=10, f3=0, rd=10, op=0x33) */
	check("E3 add a0, a0, a1",         TODO_ENCODING, 0x00b50533);

	/* E4: lui a0, 0x12345    (U-type: imm[31:12]=0x12345, rd=10, op=0x37) */
	check("E4 lui a0, 0x12345",        TODO_ENCODING, 0x12345537);

	/* E5: jalr zero, ra, 0   (the standard return pattern) */
	check("E5 jalr zero, ra, 0",       TODO_ENCODING, 0x00008067);

	/* E6: ecall */
	check("E6 ecall",                  TODO_ENCODING, 0x00000073);

	fprintf(stderr, "\n%d/%d tests passed.\n", pass_count, total_count);
	return pass_count == total_count ? 0 : 1;
}
