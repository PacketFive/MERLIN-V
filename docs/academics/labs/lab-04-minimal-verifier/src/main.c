// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * lab-04/src/main.c — CLI driver for the verifier.
 *
 * PROVIDED — do not modify.
 *
 * Usage:
 *
 *   merlin-verify <object.bin>
 *
 * Exit 0 on accept, exit 1 on reject.  Reads either a raw binary or
 * an Elf32 with a ".text" section.
 */

#define _POSIX_C_SOURCE 200809L

#include "verify.h"
#include "elf.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/stat.h>

static int load_or_die(const char *path, const uint8_t **out, size_t *out_sz,
		       struct lab_elf *e, uint8_t **raw, size_t *raw_sz)
{
	int fd = open(path, O_RDONLY);
	struct stat st;
	uint8_t magic[4];

	if (fd < 0 || fstat(fd, &st) < 0) { close(fd); return -1; }
	if (pread(fd, magic, 4, 0) != 4) { close(fd); return -1; }
	close(fd);

	if (memcmp(magic, "\x7f""ELF", 4) == 0) {
		if (lab_elf_open(path, e) < 0)
			return -1;
		*out    = e->text;
		*out_sz = e->text_size;
		return 0;
	}

	fd = open(path, O_RDONLY);
	if (fd < 0) return -1;
	void *p = mmap(NULL, st.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
	close(fd);
	if (p == MAP_FAILED) return -1;
	*raw     = p;
	*raw_sz  = st.st_size;
	*out     = p;
	*out_sz  = st.st_size;
	return 0;
}

int main(int argc, char **argv)
{
	struct verify_cfg cfg = {
		/* Helpers 1..6 permitted by default in this lab. */
		.helper_allow     = (1u << 1) | (1u << 2) | (1u << 3) |
				    (1u << 4) | (1u << 5) | (1u << 6),
		.max_stack_bytes  = 512,
		.allow_back_edges = false,
	};
	struct lab_elf e = {0};
	uint8_t *raw = NULL;
	size_t raw_sz = 0;
	const uint8_t *text;
	size_t text_sz;

	if (argc != 2) {
		fprintf(stderr, "usage: %s <object.bin>\n", argv[0]);
		return 2;
	}

	if (load_or_die(argv[1], &text, &text_sz, &e, &raw, &raw_sz) < 0) {
		fprintf(stderr, "load failed: %s\n", strerror(errno));
		return 2;
	}

	enum verify_result r = merlin_verify_text(text, text_sz, &cfg);

	if (raw) munmap(raw, raw_sz);
	lab_elf_close(&e);

	if (r == VERIFY_OK) {
		printf("ACCEPT\n");
		return 0;
	}
	printf("REJECT\n");
	return 1;
}
