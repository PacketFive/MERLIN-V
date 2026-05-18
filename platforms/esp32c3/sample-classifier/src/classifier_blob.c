/* SPDX-License-Identifier: Apache-2.0 */
/*
 * classifier_blob.c — hand-rolled Elf32 containing the packet classifier.
 *
 * Source (see classifier_src.S):
 *
 *   classify(ctx):
 *      lbu  a1, 12(a0)         ; byte at offset 12 = high byte of EtherType
 *      addi a2, x0, 8          ; 8 == high byte of 0x0800 (IPv4)
 *      beq  a1, a2, +8         ; if IPv4, jump to PASS
 *      addi a0, x0, 1          ; DROP
 *      jalr x0, ra, 0
 *   pass:
 *      addi a0, x0, 2          ; PASS
 *      jalr x0, ra, 0
 *
 * The verifier accepts this on the rtos-rv32 profile.  It exercises:
 *  - PtrCtx-rooted load        (verifier load-address check)
 *  - constant materialisation  (addi from x0)
 *  - forward branch            (back-edges rejected; forward allowed)
 *  - two return paths
 *
 * Layout (228 bytes):
 *   0   .. 51   Elf32 Ehdr
 *  52   .. 63   pad to e_shoff = 64
 *  64   .. 183  Section headers (3 * 40)
 * 184   .. 211  .text.merlin.filter.classifier  (28 bytes)
 * 212   .. end  .shstrtab
 */

#include <stdint.h>

/* clang-format off */
__attribute__((aligned(8))) const uint8_t classifier_blob[] = {
	/* ---- Elf32 Ehdr (52 bytes) ---- */
	0x7f,'E','L','F',
	1,                  /* EI_CLASS = ELFCLASS32              */
	1,                  /* EI_DATA  = ELFDATA2LSB             */
	1,                  /* EI_VERSION                         */
	0,                  /* EI_OSABI                           */
	0,                  /* EI_ABIVERSION                      */
	0,0,0,0,0,0,0,      /* EI_PAD                             */
	1,0,                /* e_type     = ET_REL                */
	0xf3,0,             /* e_machine  = EM_RISCV (243)        */
	1,0,0,0,            /* e_version                          */
	0,0,0,0,            /* e_entry                            */
	0,0,0,0,            /* e_phoff                            */
	0x40,0,0,0,         /* e_shoff   = 64                     */
	0,0,0,0,            /* e_flags                            */
	52,0,               /* e_ehsize                           */
	0,0,                /* e_phentsize                        */
	0,0,                /* e_phnum                            */
	40,0,               /* e_shentsize                        */
	3,0,                /* e_shnum   = 3                      */
	2,0,                /* e_shstrndx = 2                     */

	/* pad to e_shoff (64) */
	0,0,0,0, 0,0,0,0, 0,0,0,0,

	/* ---- Section headers ---- */

	/* [0] NULL */
	0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0,
	0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0,
	0,0,0,0, 0,0,0,0,

	/* [1] .text.merlin.filter.classifier (offset=184, size=28) */
	1,0,0,0,            /* sh_name (offset 1 in shstrtab)     */
	1,0,0,0,            /* sh_type = SHT_PROGBITS             */
	6,0,0,0,            /* sh_flags = ALLOC|EXEC              */
	0,0,0,0,            /* sh_addr                            */
	0xb8,0,0,0,         /* sh_offset = 184                    */
	28,0,0,0,           /* sh_size = 28                       */
	0,0,0,0,            /* sh_link                            */
	0,0,0,0,            /* sh_info                            */
	4,0,0,0,            /* sh_addralign = 4                   */
	0,0,0,0,            /* sh_entsize                         */

	/* [2] .shstrtab (offset=212, size=42) */
	32,0,0,0,           /* sh_name (offset 32 in shstrtab)    */
	3,0,0,0,            /* sh_type = SHT_STRTAB               */
	0,0,0,0,            /* sh_flags                           */
	0,0,0,0,            /* sh_addr                            */
	0xd4,0,0,0,         /* sh_offset = 212                    */
	42,0,0,0,           /* sh_size = 42                       */
	0,0,0,0,            /* sh_link                            */
	0,0,0,0,            /* sh_info                            */
	1,0,0,0,            /* sh_addralign = 1                   */
	0,0,0,0,            /* sh_entsize                         */

	/* ---- Section data ---- */

	/* offset 184: .text.merlin.filter.classifier (28 bytes) */
	0x83,0x45,0xc5,0x00,   /* pc=0  lbu  a1, 12(a0)              */
	0x13,0x06,0x80,0x00,   /* pc=4  addi a2, x0, 8               */
	0x63,0x84,0xc5,0x00,   /* pc=8  beq  a1, a2, +8              */
	0x13,0x05,0x10,0x00,   /* pc=12 addi a0, x0, 1   (DROP)      */
	0x67,0x80,0x00,0x00,   /* pc=16 jalr x0, ra, 0               */
	0x13,0x05,0x20,0x00,   /* pc=20 addi a0, x0, 2   (PASS)      */
	0x67,0x80,0x00,0x00,   /* pc=24 jalr x0, ra, 0               */

	/* offset 212: .shstrtab (42 bytes)
	 *   "\0.text.merlin.filter.classifier\0.shstrtab\0"
	 *      ^0   ^1                              ^31
	 */
	0,
	'.','t','e','x','t','.','m','e','r','l','i','n','.',
	'f','i','l','t','e','r','.',
	'c','l','a','s','s','i','f','i','e','r',0,
	'.','s','h','s','t','r','t','a','b',0,
};
/* clang-format on */

const uint32_t classifier_blob_len = sizeof(classifier_blob);
