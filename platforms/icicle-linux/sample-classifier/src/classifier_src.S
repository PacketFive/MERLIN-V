/* SPDX-License-Identifier: Apache-2.0 */
/*
 * classifier_src.S — readable RV64 source for the packet classifier.
 *
 * Assemble (offline) with:
 *
 *   riscv64-unknown-linux-gnu-gcc -march=rv64imac_zicsr_zifencei \
 *       -mabi=lp64 -nostartfiles -nostdlib -c classifier_src.S \
 *       -o classifier.o
 *
 * Inspect:
 *
 *   riscv64-unknown-linux-gnu-objdump -d classifier.o
 *
 * The output should match the bytes in classifier_blob.c byte-for-byte:
 *
 *   0:   00c54583    lbu  a1, 12(a0)
 *   4:   00800613    addi a2, x0, 8
 *   8:   00c58463    beq  a1, a2, .pass
 *   c:   00100513    addi a0, x0, 1
 *  10:   00008067    jalr x0, ra, 0
 *  14:   00200513    addi a0, x0, 2
 *  18:   00008067    jalr x0, ra, 0
 *
 * Note: these particular instructions encode identically on RV32 and
 * RV64.  Only the ELF container differs (Elf32 vs. Elf64), which is
 * why classifier_blob.c in this directory is the RV64 version even
 * though the .text bytes match the ESP32-C3 RV32 version.
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
