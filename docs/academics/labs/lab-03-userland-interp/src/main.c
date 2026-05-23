// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * lab-03/src/main.c — CLI driver for the user-space interpreter.
 *
 * PROVIDED — do not modify.
 *
 * Usage:
 *
 *   merlin [-t] [-a INITIAL_A0] <object.bin>
 *
 *   -t            Trace each instruction to stderr.
 *   -a INITIAL_A0 Pass INITIAL_A0 as the program's a0 (decimal or 0x...).
 *
 * For these labs, <object.bin> may be either:
 *   - A raw RV32 binary (just the .text bytes).
 *   - A relocatable ELF object (".text" section is used).
 *
 * Distinguishes the two by the ELF magic in the first 4 bytes.
 */

#define _POSIX_C_SOURCE 200809L

#include "elf.h"
#include "interp.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <getopt.h>

static int load_raw_or_elf(const char *path,
			   uint8_t **buf_out, size_t *size_out,
			   const uint8_t **text_out, size_t *text_size_out,
			   uint32_t *text_addr_out,
			   struct lab_elf *e)
{
	int fd = open(path, O_RDONLY);
	struct stat st;
	uint8_t magic[4];

	if (fd < 0) {
		fprintf(stderr, "cannot open %s: %s\n", path, strerror(errno));
		return -1;
	}
	if (fstat(fd, &st) < 0 || st.st_size < 4) {
		fprintf(stderr, "stat or too small\n");
		close(fd);
		return -1;
	}
	if (pread(fd, magic, 4, 0) != 4) {
		close(fd);
		return -1;
	}
	close(fd);

	if (memcmp(magic, "\x7f""ELF", 4) == 0) {
		if (lab_elf_open(path, e) < 0)
			return -1;
		*text_out      = e->text;
		*text_size_out = e->text_size;
		*text_addr_out = e->text_addr;
		return 0;
	}

	/* Raw binary: mmap and use the whole file. */
	{
		int rfd = open(path, O_RDONLY);
		void *p;
		if (rfd < 0)
			return -1;
		p = mmap(NULL, st.st_size, PROT_READ, MAP_PRIVATE, rfd, 0);
		close(rfd);
		if (p == MAP_FAILED)
			return -1;
		*buf_out       = p;
		*size_out      = st.st_size;
		*text_out      = p;
		*text_size_out = st.st_size;
		*text_addr_out = 0;
		return 0;
	}
}

int main(int argc, char **argv)
{
	bool trace = false;
	uint32_t a0 = 0;
	int opt;
	struct interp_state s;
	struct lab_elf e = {0};
	uint8_t *raw_buf = NULL;
	size_t raw_size = 0;
	const uint8_t *text;
	size_t text_size;
	uint32_t text_addr;
	int rc;

	while ((opt = getopt(argc, argv, "ta:")) != -1) {
		switch (opt) {
		case 't': trace = true; break;
		case 'a': a0 = (uint32_t)strtoul(optarg, NULL, 0); break;
		default:
			fprintf(stderr,
				"usage: %s [-t] [-a INITIAL_A0] <object>\n",
				argv[0]);
			return 2;
		}
	}
	if (optind >= argc) {
		fprintf(stderr, "usage: %s [-t] [-a INITIAL_A0] <object>\n",
			argv[0]);
		return 2;
	}

	if (load_raw_or_elf(argv[optind], &raw_buf, &raw_size,
			    &text, &text_size, &text_addr, &e) < 0)
		return 2;

	interp_init(&s, text, text_size, text_addr, a0);
	s.trace = trace;

	rc = interp_run(&s);
	if (rc < 0) {
		fprintf(stderr, "interp: aborted (steps=%u)\n", s.steps);
		if (raw_buf) munmap(raw_buf, raw_size);
		lab_elf_close(&e);
		return 1;
	}

	printf("%u\n", s.exit_value);

	if (raw_buf) munmap(raw_buf, raw_size);
	lab_elf_close(&e);
	return 0;
}
