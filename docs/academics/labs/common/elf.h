/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * common/elf.h — minimal Elf32 reader for the MERLIN-V labs.
 *
 * PROVIDED — do not modify.
 *
 * Reads a relocatable RISC-V Elf32 (ET_REL, EM_RISCV) blob from a
 * file, locates the first SHT_PROGBITS section whose name starts
 * with ".text.merlin." OR ".text" (the simpler labs use plain ".text"
 * sections; the later labs use the full naming scheme), and exposes
 * its bytes as (data, size).
 */
#ifndef MERLIN_LABS_ELF_H
#define MERLIN_LABS_ELF_H

#include <stddef.h>
#include <stdint.h>

struct lab_elf {
	uint8_t      *blob;         /* mmap'd file contents */
	size_t        blob_size;
	const uint8_t *text;        /* pointer into blob; do not free */
	size_t        text_size;
	uint32_t      text_addr;    /* sh_addr (often 0 for ET_REL)   */
	char          section_name[64];
};

/* Load and parse. Returns 0 on success; on failure prints to stderr
 * and returns -1.  Caller frees with lab_elf_close().
 */
int  lab_elf_open(const char *path, struct lab_elf *out);
void lab_elf_close(struct lab_elf *e);

#endif /* MERLIN_LABS_ELF_H */
