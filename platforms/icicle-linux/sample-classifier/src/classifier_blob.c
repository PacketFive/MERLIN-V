/* SPDX-License-Identifier: Apache-2.0 */
/*
 * classifier_blob.c — hand-rolled Elf64 (RV64) packet classifier.
 *
 * Same RV instruction stream as the ESP32-C3 sample (the encodings
 * are identical for these specific instructions on RV32 and RV64),
 * wrapped in an Elf64 container so the Linux kernel loader accepts
 * it as a 64-bit RISC-V relocatable object.
 *
 * Bytecode:
 *
 *   lbu  a1, 12(a0)       ; a1 = pkt[12] (EtherType high byte)
 *   addi a2, x0, 8        ; a2 = 0x08 (IPv4 high byte)
 *   beq  a1, a2, .pass    ; jump fwd 8 if IPv4
 *   addi a0, x0, 1        ; return 1 (DROP)
 *   jalr x0, ra, 0
 * .pass:
 *   addi a0, x0, 2        ; return 2 (PASS)
 *   jalr x0, ra, 0
 *
 * Layout (326 bytes):
 *    0 ..  63    Elf64 Ehdr
 *   64 .. 255    Section headers (3 * 64)
 *  256 .. 283    .text.merlin.filter.classifier (28 bytes)
 *  284 .. 325    .shstrtab (42 bytes)
 */

#include <stdint.h>

/* clang-format off */
__attribute__((aligned(8))) const uint8_t classifier_blob[] = {
	/* ---- Elf64 Ehdr (64 bytes) ---- */
	0x7f,'E','L','F',
	2,                  /* EI_CLASS = ELFCLASS64               */
	1,                  /* EI_DATA  = ELFDATA2LSB              */
	1,                  /* EI_VERSION                          */
	0,                  /* EI_OSABI                            */
	0,                  /* EI_ABIVERSION                       */
	0,0,0,0,0,0,0,      /* EI_PAD (bytes 9-15)                 */
	1,0,                /* e_type    = ET_REL                  */
	0xf3,0,             /* e_machine = EM_RISCV (243)          */
	1,0,0,0,            /* e_version                           */
	0,0,0,0,0,0,0,0,    /* e_entry (u64)                       */
	0,0,0,0,0,0,0,0,    /* e_phoff (u64)                       */
	0x40,0,0,0,0,0,0,0, /* e_shoff = 64                        */
	0,0,0,0,            /* e_flags                             */
	64,0,               /* e_ehsize                            */
	0,0,                /* e_phentsize                         */
	0,0,                /* e_phnum                             */
	64,0,               /* e_shentsize = 64 (Elf64_Shdr)       */
	3,0,                /* e_shnum                             */
	2,0,                /* e_shstrndx                          */

	/* ---- Section headers (3 * 64 bytes = 192) ---- */

	/* [0] NULL section — all zeros */
	0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0,
	0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0,
	0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0,
	0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0,

	/* [1] .text.merlin.filter.classifier  (offset=256, size=28)
	 * Elf64_Shdr fields:
	 *   sh_name (4)   sh_type (4)   sh_flags (8)   sh_addr (8)
	 *   sh_offset(8)  sh_size (8)   sh_link (4)    sh_info (4)
	 *   sh_addralign(8) sh_entsize(8)
	 */
	1,0,0,0,                       /* sh_name = 1                */
	1,0,0,0,                       /* sh_type = SHT_PROGBITS     */
	6,0,0,0,0,0,0,0,               /* sh_flags = ALLOC|EXEC      */
	0,0,0,0,0,0,0,0,               /* sh_addr                    */
	0x00,0x01,0,0,0,0,0,0,         /* sh_offset = 256            */
	28,0,0,0,0,0,0,0,              /* sh_size = 28               */
	0,0,0,0,                       /* sh_link                    */
	0,0,0,0,                       /* sh_info                    */
	4,0,0,0,0,0,0,0,               /* sh_addralign = 4           */
	0,0,0,0,0,0,0,0,               /* sh_entsize                 */

	/* [2] .shstrtab  (offset=284, size=42) */
	32,0,0,0,                      /* sh_name = 32               */
	3,0,0,0,                       /* sh_type = SHT_STRTAB       */
	0,0,0,0,0,0,0,0,               /* sh_flags                   */
	0,0,0,0,0,0,0,0,               /* sh_addr                    */
	0x1c,0x01,0,0,0,0,0,0,         /* sh_offset = 284            */
	42,0,0,0,0,0,0,0,              /* sh_size = 42               */
	0,0,0,0,                       /* sh_link                    */
	0,0,0,0,                       /* sh_info                    */
	1,0,0,0,0,0,0,0,               /* sh_addralign = 1           */
	0,0,0,0,0,0,0,0,               /* sh_entsize                 */

	/* ---- Section data ---- */

	/* offset 256: .text.merlin.filter.classifier (28 bytes) */
	0x83,0x45,0xc5,0x00,   /* pc=0  lbu  a1, 12(a0)              */
	0x13,0x06,0x80,0x00,   /* pc=4  addi a2, x0, 8               */
	0x63,0x84,0xc5,0x00,   /* pc=8  beq  a1, a2, +8              */
	0x13,0x05,0x10,0x00,   /* pc=12 addi a0, x0, 1   (DROP)      */
	0x67,0x80,0x00,0x00,   /* pc=16 jalr x0, ra, 0               */
	0x13,0x05,0x20,0x00,   /* pc=20 addi a0, x0, 2   (PASS)      */
	0x67,0x80,0x00,0x00,   /* pc=24 jalr x0, ra, 0               */

	/* offset 284: .shstrtab (42 bytes) */
	0,
	'.','t','e','x','t','.','m','e','r','l','i','n','.',
	'f','i','l','t','e','r','.',
	'c','l','a','s','s','i','f','i','e','r',0,
	'.','s','h','s','t','r','t','a','b',0,
};
/* clang-format on */

const uint32_t classifier_blob_len = sizeof(classifier_blob);
