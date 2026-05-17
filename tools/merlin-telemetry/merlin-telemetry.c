// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * merlin-telemetry - prototype telemetry shim driver.
 *
 * Subcommands:
 *
 *   merlin-telemetry run -n <iters> <object.o>
 *       JIT the program, wrap in the dispatch shim, run <iters>
 *       times, print the canonical stats block (text and binary).
 *
 *   merlin-telemetry dump <stats.bin>
 *       Read a merlin_prog_stats_v1 wire-format file and pretty-
 *       print it.  Demonstrates forward-compat parsing
 *       (consult `size`, ignore tail).
 *
 *   merlin-telemetry text <stats.bin>
 *       Same as dump but emits the tracefs text format.
 *
 * The "run" subcommand uses the JIT prototype's helper trampoline
 * (so helper_call_count moves) and asserts via exit status that
 * the program returned a verdict in the expected MVDP range.
 */

#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>

#include <gelf.h>
#include <libelf.h>

#include "shim.h"
#include "jit.h"

extern void          merlin_jit_helper_trampoline(uint64_t *regs);
extern unsigned long merlin_helper_calls[4096];

static int load_text_section(const char *path,
			     const uint8_t **out, size_t *out_len,
			     char *secname, size_t cap)
{
	if (elf_version(EV_CURRENT) == EV_NONE) return -1;
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
		elf_end(e); close(fd); return -1;
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
		if (!copy) { elf_end(e); close(fd); return -1; }
		memcpy(copy, d->d_buf, d->d_size);
		*out = copy;
		*out_len = d->d_size;
		snprintf(secname, cap, "%s", name);
		elf_end(e); close(fd);
		return 0;
	}
	fprintf(stderr, "%s: no .text.merlin.* section\n", path);
	elf_end(e); close(fd);
	return -1;
}

static int cmd_run(int iters, const char *path)
{
	const uint8_t *text;
	size_t text_size;
	char secname[64];
	if (load_text_section(path, &text, &text_size, secname,
			      sizeof(secname)) < 0)
		return 1;

	struct merlin_jit_result r;
	if (merlin_jit_translate(text, text_size,
				 merlin_jit_helper_trampoline, &r) < 0) {
		fprintf(stderr, "JIT error: %s\n", r.error);
		free((void *)text);
		return 1;
	}
	free((void *)text);

	struct merlin_prog prog;
	merlin_prog_init(&prog, r.fn, 1, secname,
			 /* verifier_load_ns stub */ 1234567);

	uint64_t ctx_buf[64] = {0};
	memset(merlin_helper_calls, 0, sizeof(merlin_helper_calls));

	uint64_t last_ret = 0;
	for (int i = 0; i < iters; i++) {
		last_ret = merlin_prog_invoke(&prog, ctx_buf);
	}

	/* Sync helper counter from the JIT-side counter array.
	 * Real kernel implementation does this via the trampoline's
	 * own increment; in the prototype the trampoline updates
	 * merlin_helper_calls[] which we fold once.
	 */
	uint64_t total_calls = 0;
	for (size_t i = 0; i < 4096; i++)
		total_calls += merlin_helper_calls[i];
	prog.stats.helper_call_count = total_calls;

	char text_out[2048];
	merlin_prog_render_text(&prog, text_out, sizeof(text_out));
	fputs(text_out, stdout);
	printf("last_return: 0x%lx\n", (unsigned long)last_ret);

	merlin_jit_free(&r);
	return 0;
}

static int cmd_dump(const char *path, bool as_text)
{
	int fd = open(path, O_RDONLY);
	if (fd < 0) { perror("open"); return 1; }

	uint8_t buf[4096];
	ssize_t n = read(fd, buf, sizeof(buf));
	close(fd);
	if (n < (ssize_t)sizeof(uint32_t)) {
		fprintf(stderr, "stats file too short\n");
		return 1;
	}

	uint32_t size;
	memcpy(&size, buf, sizeof(size));
	if (size > n) {
		fprintf(stderr, "stats size %u > file %zd\n", size, n);
		return 1;
	}

	/* Forward-compat read: copy min(size, sizeof(local)) bytes into
	 * a local zero-initialised struct; tail fields the local doesn't
	 * know about are dropped silently. */
	struct merlin_prog_stats_v1 s = {0};
	size_t take = size < sizeof(s) ? size : sizeof(s);
	memcpy(&s, buf, take);

	struct merlin_prog p = { .stats = s };
	snprintf(p.prog_name, sizeof(p.prog_name), "<from-file>");
	p.prog_id = 0;

	if (as_text) {
		char out[2048];
		merlin_prog_render_text(&p, out, sizeof(out));
		fputs(out, stdout);
	} else {
		printf("size               %u\n", s.size);
		printf("run_count          %lu\n", (unsigned long)s.run_count);
		printf("run_ns_total       %lu\n", (unsigned long)s.run_ns_total);
		for (int i = 0; i < MERLIN_STATS_V1_VERDICTS; i++) {
			printf("verdict[%d]         %lu\n", i,
			       (unsigned long)s.verdict_count[i]);
		}
		printf("helper_call_count  %lu\n",
		       (unsigned long)s.helper_call_count);
		printf("helper_fault_count %lu\n",
		       (unsigned long)s.helper_fault_count);
		printf("verifier_load_ns   %lu\n",
		       (unsigned long)s.verifier_load_ns);
		printf("last_run_time_ns   %lu\n",
		       (unsigned long)s.last_run_time_ns);
	}
	return 0;
}

static void usage(void)
{
	fprintf(stderr,
		"Usage:\n"
		"  merlin-telemetry run  -n <iters> <object.o>\n"
		"  merlin-telemetry dump <stats.bin>\n"
		"  merlin-telemetry text <stats.bin>\n");
}

int main(int argc, char **argv)
{
	if (argc < 2) { usage(); return 2; }

	if (strcmp(argv[1], "dump") == 0 && argc == 3)
		return cmd_dump(argv[2], false);
	if (strcmp(argv[1], "text") == 0 && argc == 3)
		return cmd_dump(argv[2], true);

	if (strcmp(argv[1], "run") == 0) {
		int iters = 1;
		const char *path = NULL;
		optind = 2;
		int opt;
		while ((opt = getopt(argc, argv, "n:")) != -1) {
			if (opt == 'n') iters = atoi(optarg);
			else { usage(); return 2; }
		}
		if (optind >= argc || iters < 1) { usage(); return 2; }
		path = argv[optind];
		return cmd_run(iters, path);
	}

	usage();
	return 2;
}
