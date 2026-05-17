// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * merlin-verifier - prototype user-space verifier.
 *
 * Loads a MERLIN-V ELF object, finds the .text.merlin.<type>.<name>
 * sections, decodes their instructions, and runs the abstract-
 * interpretation verifier (verify.c).  Diagnostics on stderr; one
 * summary line per program on stdout.
 *
 * Usage:
 *
 *     merlin-verifier <object.o>             # one-line summary per program
 *     merlin-verifier -v <object.o>          # verbose; per-insn trace
 *     merlin-verifier -p mvdp <object.o>     # force program-type allowlist
 *
 * Exit codes:
 *
 *    0  every program in the object verified OK
 *    1  at least one program rejected
 *    2  CLI / I/O error
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

#include "verify.h"

#define die(fmt, ...) do {                                         \
	fprintf(stderr, "merlin-verifier: " fmt "\n", ##__VA_ARGS__);\
	exit(2);                                                   \
} while (0)

/* ---- ELF helpers (parallel to merlin-objtool) ---- */

struct elf_obj {
	int       fd;
	Elf      *elf;
	GElf_Ehdr ehdr;
	size_t    shstrndx;
};

static void elf_obj_close(struct elf_obj *o)
{
	if (o->elf) elf_end(o->elf);
	if (o->fd >= 0) close(o->fd);
}

static int elf_obj_open(const char *path, struct elf_obj *o)
{
	memset(o, 0, sizeof(*o));
	o->fd = -1;

	if (elf_version(EV_CURRENT) == EV_NONE)
		return -1;

	o->fd = open(path, O_RDONLY);
	if (o->fd < 0) {
		fprintf(stderr, "open %s: %s\n", path, strerror(errno));
		return -1;
	}
	o->elf = elf_begin(o->fd, ELF_C_READ, NULL);
	if (!o->elf || elf_kind(o->elf) != ELF_K_ELF) {
		fprintf(stderr, "%s: not an ELF object\n", path);
		goto fail;
	}
	if (!gelf_getehdr(o->elf, &o->ehdr))                 goto fail;
	if (elf_getshdrstrndx(o->elf, &o->shstrndx))         goto fail;
	return 0;
fail:
	elf_obj_close(o);
	return -1;
}

/* ---- per-program verification ---- */

static int verify_one(struct elf_obj *o, Elf_Scn *scn,
		      const char *sname,
		      const struct merlin_verifier_cfg *cfg)
{
	Elf_Data *d = elf_getdata(scn, NULL);
	if (!d || !d->d_buf || d->d_size == 0) {
		fprintf(stderr, "%s: empty section\n", sname);
		return 1;
	}

	GElf_Shdr shdr;
	gelf_getshdr(scn, &shdr);

	char summary[256] = {0};
	enum merlin_verify_result rc = merlin_verify_text(
		d->d_buf, d->d_size,
		(uint32_t)shdr.sh_offset,
		cfg, summary, sizeof(summary));

	const char *verdict = (rc == MERLIN_VERIFY_OK) ? "ACCEPT" : "REJECT";
	printf("%s  %s  (%zu bytes)  %s\n",
	       verdict, sname, d->d_size, summary);

	return rc == MERLIN_VERIFY_OK ? 0 : 1;
}

/* ---- main ---- */

static void usage(void)
{
	fprintf(stderr,
		"Usage:\n"
		"  merlin-verifier [-v] [-p mvdp] <object.o>\n"
		"\n"
		"Options:\n"
		"  -v         verbose (per-insn trace to stderr)\n"
		"  -p <type>  program-type helper allowlist (default: mvdp)\n");
}

int main(int argc, char **argv)
{
	struct merlin_verifier_cfg cfg;
	merlin_verifier_cfg_init_for_mvdp(&cfg);

	int opt;
	const char *prog_type = "mvdp";

	while ((opt = getopt(argc, argv, "vp:h")) != -1) {
		switch (opt) {
		case 'v': cfg.verbose = true; break;
		case 'p': prog_type = optarg; break;
		case 'h': usage(); return 0;
		default:  usage(); return 2;
		}
	}
	if (optind >= argc) {
		usage();
		return 2;
	}
	const char *path = argv[optind];

	if (strcmp(prog_type, "mvdp") != 0) {
		fprintf(stderr,
			"merlin-verifier: only prog_type=mvdp is supported "
			"in this prototype (got '%s')\n", prog_type);
		return 2;
	}

	struct elf_obj o;
	if (elf_obj_open(path, &o) < 0)
		die("could not open %s", path);

	if (o.ehdr.e_ident[EI_DATA] != ELFDATA2LSB) {
		fprintf(stderr, "%s: ELF is not little-endian\n", path);
		elf_obj_close(&o);
		return 2;
	}

	int rejects = 0;
	int progs   = 0;
	Elf_Scn *scn = NULL;
	while ((scn = elf_nextscn(o.elf, scn)) != NULL) {
		GElf_Shdr sh;
		if (!gelf_getshdr(scn, &sh))
			continue;
		const char *sname = elf_strptr(o.elf, o.shstrndx, sh.sh_name);
		if (!sname || strncmp(sname, ".text.merlin.", 13) != 0)
			continue;

		progs++;
		if (verify_one(&o, scn, sname, &cfg) != 0)
			rejects++;
	}

	if (progs == 0) {
		fprintf(stderr,
			"%s: no .text.merlin.* sections found; "
			"is this a MERLIN-V object?\n",
			path);
		elf_obj_close(&o);
		return 2;
	}

	elf_obj_close(&o);
	return rejects ? 1 : 0;
}
