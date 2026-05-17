// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * merlin-jit - prototype host JIT driver.
 *
 * Loads a MERLIN-V ELF object, finds the first .text.merlin.* section,
 * JITs it to x86_64, and runs it.  Prints the return value plus the
 * helper-call histogram.
 *
 * Usage:
 *
 *     merlin-jit <object.o>
 *     merlin-jit -d <object.o>          # dump emitted x86 bytes
 */

#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>

#include <gelf.h>
#include <libelf.h>

#include "jit.h"

extern void merlin_jit_helper_trampoline(uint64_t *regs);
extern unsigned long merlin_helper_calls[4096];

static int load_section(const char *path,
			const uint8_t **text_out, size_t *len_out,
			char *secname_out, size_t secname_cap)
{
	if (elf_version(EV_CURRENT) == EV_NONE)
		return -1;
	int fd = open(path, O_RDONLY);
	if (fd < 0) {
		fprintf(stderr, "open %s: %s\n", path, strerror(errno));
		return -1;
	}
	Elf *e = elf_begin(fd, ELF_C_READ, NULL);
	if (!e || elf_kind(e) != ELF_K_ELF) {
		fprintf(stderr, "%s: not an ELF object\n", path);
		if (e) elf_end(e);
		close(fd);
		return -1;
	}

	size_t shstrndx;
	if (elf_getshdrstrndx(e, &shstrndx) < 0) {
		fprintf(stderr, "%s: no section string table\n", path);
		elf_end(e); close(fd);
		return -1;
	}

	Elf_Scn *scn = NULL;
	while ((scn = elf_nextscn(e, scn)) != NULL) {
		GElf_Shdr sh;
		if (!gelf_getshdr(scn, &sh)) continue;
		const char *name = elf_strptr(e, shstrndx, sh.sh_name);
		if (!name || strncmp(name, ".text.merlin.", 13) != 0)
			continue;

		Elf_Data *d = elf_getdata(scn, NULL);
		if (!d || !d->d_buf || d->d_size == 0) continue;

		uint8_t *copy = malloc(d->d_size);
		if (!copy) {
			elf_end(e); close(fd);
			return -1;
		}
		memcpy(copy, d->d_buf, d->d_size);
		*text_out = copy;
		*len_out  = d->d_size;
		snprintf(secname_out, secname_cap, "%s", name);

		elf_end(e);
		close(fd);
		return 0;
	}

	fprintf(stderr, "%s: no .text.merlin.* section found\n", path);
	elf_end(e);
	close(fd);
	return -1;
}

static void hexdump(const uint8_t *p, size_t n)
{
	for (size_t i = 0; i < n; i++) {
		if (i % 16 == 0)
			printf("%04zx:", i);
		printf(" %02x", p[i]);
		if (i % 16 == 15 || i == n - 1)
			printf("\n");
	}
}

static void usage(void)
{
	fprintf(stderr,
		"Usage:\n"
		"  merlin-jit       <object.o>\n"
		"  merlin-jit -d    <object.o>     # dump emitted x86_64 bytes\n");
}

int main(int argc, char **argv)
{
	bool dump = false;
	int opt;
	while ((opt = getopt(argc, argv, "dh")) != -1) {
		switch (opt) {
		case 'd': dump = true; break;
		case 'h': usage(); return 0;
		default:  usage(); return 2;
		}
	}
	if (optind >= argc) { usage(); return 2; }

	const char *path = argv[optind];

	const uint8_t *text;
	size_t text_size;
	char secname[64];
	if (load_section(path, &text, &text_size, secname, sizeof(secname)) < 0)
		return 1;

	printf("JIT: %s  (.text size %zu bytes)\n", secname, text_size);

	struct merlin_jit_result r;
	if (merlin_jit_translate(text, text_size,
				 merlin_jit_helper_trampoline, &r) < 0) {
		fprintf(stderr, "JIT error: %s\n", r.error);
		free((void *)text);
		return 1;
	}

	printf("  emitted %zu host bytes at %p\n", r.code_bytes, r.exec);

	if (dump) {
		printf("  --- x86_64 ---\n");
		hexdump((const uint8_t *)r.exec, r.code_bytes);
	}

	/* Run it.  Pass a stub ctx pointer; the program may write into
	 * regs[10] but won't actually deref ctx unless the program does
	 * so explicitly (and the verifier would have flagged unsafe
	 * derefs).
	 */
	uint64_t ctx_buf[64] = {0};
	memset(merlin_helper_calls, 0, sizeof(merlin_helper_calls));

	uint64_t ret = r.fn(ctx_buf);

	printf("  return: 0x%lx (decimal %ld)\n",
	       (unsigned long)ret, (long)(int64_t)ret);

	int any = 0;
	for (size_t i = 0; i < 4096; i++) {
		if (merlin_helper_calls[i]) {
			if (!any) {
				printf("  helper calls:\n");
				any = 1;
			}
			printf("    id 0x%03zx called %lu times\n",
			       i, merlin_helper_calls[i]);
		}
	}

	merlin_jit_free(&r);
	free((void *)text);
	return 0;
}
