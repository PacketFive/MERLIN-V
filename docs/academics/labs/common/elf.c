// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * common/elf.c — minimal Elf32 reader, used by the MERLIN-V labs.
 *
 * PROVIDED — do not modify.
 */
#include "elf.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/stat.h>

/* Packed Elf32 structs — kept self-contained so the labs build on
 * any host without <elf.h>.
 */
struct lab_eh {
	uint8_t  ident[16];
	uint16_t type, machine;
	uint32_t version, entry, phoff, shoff, flags;
	uint16_t ehsize, phentsize, phnum, shentsize, shnum, shstrndx;
} __attribute__((packed));

struct lab_sh {
	uint32_t name, type, flags, addr, off, size, link, info, al, es;
} __attribute__((packed));

#define ELFMAG    "\x7f""ELF"
#define ELFCLASS32 1
#define ELFDATA2LSB 1
#define ET_REL    1
#define EM_RISCV  243
#define SHT_PROGBITS 1
#define SHT_STRTAB   3

int lab_elf_open(const char *path, struct lab_elf *out)
{
	int fd, rc = -1;
	struct stat st;
	uint8_t *blob = NULL;
	const struct lab_eh *eh;
	const struct lab_sh *shstr;
	const char *names;

	memset(out, 0, sizeof(*out));

	fd = open(path, O_RDONLY);
	if (fd < 0) {
		fprintf(stderr, "lab_elf_open: cannot open %s: %s\n",
			path, strerror(errno));
		return -1;
	}
	if (fstat(fd, &st) < 0 || st.st_size < (off_t)sizeof(*eh)) {
		fprintf(stderr, "lab_elf_open: stat failed or too small\n");
		goto out_close;
	}
	blob = mmap(NULL, st.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
	if (blob == MAP_FAILED) {
		fprintf(stderr, "lab_elf_open: mmap: %s\n", strerror(errno));
		blob = NULL;
		goto out_close;
	}
	out->blob = blob;
	out->blob_size = st.st_size;

	eh = (const struct lab_eh *)blob;
	if (memcmp(eh->ident, ELFMAG, 4) ||
	    eh->ident[4] != ELFCLASS32 ||
	    eh->ident[5] != ELFDATA2LSB) {
		fprintf(stderr, "lab_elf_open: not an LE Elf32\n");
		goto out_unmap;
	}
	if (eh->type != ET_REL || eh->machine != EM_RISCV) {
		fprintf(stderr, "lab_elf_open: not a relocatable RISC-V object\n");
		goto out_unmap;
	}
	if (eh->shstrndx == 0 || eh->shstrndx >= eh->shnum) {
		fprintf(stderr, "lab_elf_open: bogus shstrndx\n");
		goto out_unmap;
	}
	shstr = (const struct lab_sh *)(blob + eh->shoff +
					eh->shstrndx * eh->shentsize);
	if (shstr->type != SHT_STRTAB) {
		fprintf(stderr, "lab_elf_open: shstrtab is wrong type\n");
		goto out_unmap;
	}
	names = (const char *)(blob + shstr->off);

	for (unsigned i = 1; i < eh->shnum; i++) {
		const struct lab_sh *sh = (const struct lab_sh *)
			(blob + eh->shoff + i * eh->shentsize);
		const char *sname = names + sh->name;

		if (sh->type != SHT_PROGBITS)
			continue;
		/* Accept either ".text.merlin.*" or ".text" */
		if (strncmp(sname, ".text.merlin.", 13) == 0 ||
		    strcmp (sname, ".text") == 0) {
			out->text      = blob + sh->off;
			out->text_size = sh->size;
			out->text_addr = sh->addr;
			snprintf(out->section_name, sizeof(out->section_name),
				 "%s", sname);
			break;
		}
	}
	if (!out->text) {
		fprintf(stderr,
			"lab_elf_open: no .text or .text.merlin.* section\n");
		goto out_unmap;
	}

	close(fd);
	return 0;

out_unmap:
	munmap(blob, st.st_size);
	out->blob = NULL;
out_close:
	close(fd);
	return rc;
}

void lab_elf_close(struct lab_elf *e)
{
	if (e->blob) {
		munmap(e->blob, e->blob_size);
		e->blob = NULL;
	}
}
