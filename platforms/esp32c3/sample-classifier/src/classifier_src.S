/* SPDX-License-Identifier: Apache-2.0 */
/*
 * classifier_src.S — readable RV32 source for the packet classifier.
 *
 * This is the source the hand-rolled `classifier_blob.c` mirrors.  When
 * the project's GCC/Clang toolchain integration ships, this file will
 * become the canonical input and `classifier_blob.c` will be replaced
 * by an objcopy-derived binary blob.
 *
 * Assemble (offline) with:
 *
 *   riscv32-unknown-elf-gcc -march=rv32imc -mabi=ilp32 \
 *       -nostartfiles -nostdlib -c classifier_src.S \
 *       -o classifier.o
 *
 * and inspect:
 *
 *   riscv32-unknown-elf-objdump -d classifier.o
 *
 * Expected encoding (matches classifier_blob.c byte-for-byte):
 *
 *   0:   00c54583    lbu  a1, 12(a0)
 *   4:   00800613    addi a2, x0, 8
 *   8:   00c58463    beq  a1, a2, .pass
 *   c:   00100513    addi a0, x0, 1
 *  10:   00008067    jalr x0, ra, 0
 *  14:   00200513    addi a0, x0, 2
 *  18:   00008067    jalr x0, ra, 0
 */

	.section .text.merlin.filter.classifier, "ax"
	.globl classify
	.type  classify, @function
classify:
	lbu  a1, 12(a0)         /* a1 = pkt[12] (EtherType high byte)   */
	addi a2, x0, 8          /* a2 = 0x08 (IPv4 EtherType high byte) */
	beq  a1, a2, .pass      /* IPv4? jump to PASS                    */
	addi a0, x0, 1          /* return 1 (DROP)                       */
	jalr x0, ra, 0
.pass:
	addi a0, x0, 2          /* return 2 (PASS)                       */
	jalr x0, ra, 0
	.size  classify, . - classify

/*
 * .merlin.meta section (future): declares the bytecode profile.
 *
 * For now classifier_blob.c omits .merlin.meta — the Zephyr loader
 * uses CONFIG_MERLIN profile defaults.  The kernel loader will require
 * an explicit profile tag (see docs/design/02-isa-and-bytecode.md §5).
 */
