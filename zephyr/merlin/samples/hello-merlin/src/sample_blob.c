/* SPDX-License-Identifier: Apache-2.0 */
/*
 * sample_blob.c — a hand-rolled, minimal MERLIN-V ELF.
 *
 * The blob below is a complete Elf32 (RV32) object containing a single
 * .text.merlin.filter.hello section with this RISC-V program:
 *
 *     addi a0, x0, 0x2a       # a0 = 42
 *     jalr x0, ra, 0          # return
 *
 * Encoded:
 *     0x02a00513   addi a0, x0, 42
 *     0x00008067   jalr x0, ra, 0
 *
 * The ELF metadata is the bare minimum the Zephyr loader inspects:
 * EI_MAG, EI_CLASS=32, EI_DATA=LE, e_type=ET_REL, e_machine=EM_RISCV,
 * shstrndx pointing at a .shstrtab section.
 *
 * In a real workflow this blob would be produced by `riscv32-unknown-elf-gcc`
 * with the section attributes from <merlin/section_macros.h>.  The
 * hand-rolled version is checked in so the sample builds without a
 * cross-compiler.
 */

#include <stdint.h>

/* clang-format off */
__attribute__((aligned(8))) const uint8_t sample_blob[] = {
	/* ---- Elf32 Ehdr (52 bytes) ---- */
	0x7f,'E','L','F',
	1,                  /* EI_CLASS = ELFCLASS32             */
	1,                  /* EI_DATA  = ELFDATA2LSB            */
	1,                  /* EI_VERSION                        */
	0,                  /* EI_OSABI                          */
	0,                  /* EI_ABIVERSION                     */
	0,0,0,0,0,0,0,      /* EI_PAD (bytes 9-15)               */
	1,0,                /* e_type     = ET_REL               */
	0xf3,0,             /* e_machine  = EM_RISCV (243)       */
	1,0,0,0,            /* e_version                         */
	0,0,0,0,            /* e_entry                           */
	0,0,0,0,            /* e_phoff                           */
	0x40,0,0,0,         /* e_shoff   = 64                    */
	0,0,0,0,            /* e_flags                           */
	52,0,               /* e_ehsize                          */
	0,0,                /* e_phentsize                       */
	0,0,                /* e_phnum                           */
	40,0,               /* e_shentsize                       */
	3,0,                /* e_shnum   = 3 (NULL, text, str)   */
	2,0,                /* e_shstrndx = 2                    */

	/* padding to e_shoff (64) — 64 - 52 = 12 bytes */
	0,0,0,0, 0,0,0,0, 0,0,0,0,

	/* ---- Section headers (3 * 40 bytes = 120) ---- */

	/* [0] NULL section */
	0,0,0,0,  0,0,0,0,  0,0,0,0,  0,0,0,0,
	0,0,0,0,  0,0,0,0,  0,0,0,0,  0,0,0,0,
	0,0,0,0,  0,0,0,0,

	/* [1] .text.merlin.filter.hello (offset = 184, size = 8) */
	1,0,0,0,            /* sh_name (offset 1 in shstrtab)    */
	1,0,0,0,            /* sh_type = SHT_PROGBITS            */
	6,0,0,0,            /* sh_flags = ALLOC|EXEC             */
	0,0,0,0,            /* sh_addr                           */
	0xb8,0,0,0,         /* sh_offset = 184                   */
	8,0,0,0,            /* sh_size = 8                       */
	0,0,0,0,            /* sh_link                           */
	0,0,0,0,            /* sh_info                           */
	4,0,0,0,            /* sh_addralign = 4                  */
	0,0,0,0,            /* sh_entsize                        */

	/* [2] .shstrtab (offset = 192, size = 37) */
	0x1b,0,0,0,         /* sh_name (offset 27 in shstrtab)   */
	3,0,0,0,            /* sh_type = SHT_STRTAB              */
	0,0,0,0,            /* sh_flags                          */
	0,0,0,0,            /* sh_addr                           */
	0xc0,0,0,0,         /* sh_offset = 192                   */
	37,0,0,0,           /* sh_size = 37                      */
	0,0,0,0,            /* sh_link                           */
	0,0,0,0,            /* sh_info                           */
	1,0,0,0,            /* sh_addralign = 1                  */
	0,0,0,0,            /* sh_entsize                        */

	/* ---- Section data ---- */

	/* offset 184: .text.merlin.filter.hello (8 bytes) */
	0x13,0x05,0xa0,0x02,    /* addi a0, x0, 42 (0x02a00513 LE)   */
	0x67,0x80,0x00,0x00,    /* jalr x0, ra, 0  (0x00008067 LE)   */

	/* offset 192: .shstrtab (28 bytes)
	 *   "\0.text.merlin.filter.hello\0.shstrtab\0"
	 *      ^0   ^1                          ^27
	 */
	0,
	'.','t','e','x','t','.','m','e','r','l','i','n','.',
	'f','i','l','t','e','r','.','h','e','l','l','o',0,
	'.','s','h','s','t','r','t','a','b',0,
};
/* clang-format on */

const uint32_t sample_blob_len = sizeof(sample_blob);
