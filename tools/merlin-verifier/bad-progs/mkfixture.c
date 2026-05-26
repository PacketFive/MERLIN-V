// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * mkfixture - build a minimal MERLIN-V ELF .o whose .text.merlin.mvdp.test
 * contains a hand-assembled RV64 instruction stream.
 *
 * Usage:
 *   mkfixture <out.o> <hex32> [<hex32> ...]
 *
 * Each <hex32> is a 32-bit RISC-V instruction encoded as a hex literal
 * (e.g. 0x00000073 for ecall).  The resulting object passes every
 * static check merlin-objtool performs and presents a real RV64 .text
 * to merlin-verifier.
 *
 * The fixture builder is the lowest-trust path the verifier has to
 * defend against: it emits arbitrary 32-bit words into .text.merlin.*.
 * The verifier must distinguish "valid helper-call sequence" from
 * "ecall with garbage in a7" etc.  That is exactly the property we want
 * to test.
 *
 * The output ELF has the following sections (minimum a MERLIN-V object
 * needs to pass merlin-objtool validate):
 *
 *   .merlin.meta       80-byte merlin_meta_v1
 *   .merlin.license    "GPL\0"
 *   .text.merlin.mvdp.test   the user-supplied instruction stream
 *
 * No relocs, no maps, no BTF -- the verifier doesn't require those.
 */

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>

#include <libelf.h>
#include <gelf.h>

#include "merlin/merlin.h"

#define die(fmt, ...) do { \
	fprintf(stderr, "mkfixture: " fmt "\n", ##__VA_ARGS__); \
	exit(2); \
} while (0)

static uint32_t parse_hex32(const char *s)
{
	char *end;
	errno = 0;
	unsigned long long v = strtoull(s, &end, 0);
	if (errno || *end || v > 0xFFFFFFFFull)
		die("bad hex literal: '%s'", s);
	return (uint32_t)v;
}

static Elf_Scn *add_section(Elf *e, const char *name, Elf_Data *strtab_data,
			    int sh_type, uint64_t flags,
			    const void *buf, size_t buflen)
{
	Elf_Scn *scn = elf_newscn(e);
	if (!scn) die("elf_newscn: %s", elf_errmsg(-1));

	Elf_Data *d = elf_newdata(scn);
	if (!d) die("elf_newdata: %s", elf_errmsg(-1));

	/* Allocate a copy because libelf wants to own it. */
	void *copy = malloc(buflen);
	if (!copy) die("oom");
	memcpy(copy, buf, buflen);

	d->d_buf     = copy;
	d->d_size    = buflen;
	d->d_align   = 1;
	d->d_off     = 0;
	d->d_type    = ELF_T_BYTE;
	d->d_version = EV_CURRENT;

	GElf_Shdr sh;
	if (!gelf_getshdr(scn, &sh)) die("gelf_getshdr: %s", elf_errmsg(-1));
	sh.sh_type    = sh_type;
	sh.sh_flags   = flags;
	sh.sh_size    = buflen;
	sh.sh_entsize = 0;

	/* Append name to string table. */
	size_t name_off = strtab_data->d_size;
	size_t old_size = strtab_data->d_size;
	size_t new_size = old_size + strlen(name) + 1;
	char *ns = realloc(strtab_data->d_buf, new_size);
	if (!ns) die("oom");
	memcpy(ns + old_size, name, strlen(name) + 1);
	strtab_data->d_buf  = ns;
	strtab_data->d_size = new_size;

	sh.sh_name = (uint32_t)name_off;
	if (!gelf_update_shdr(scn, &sh)) die("gelf_update_shdr: %s", elf_errmsg(-1));

	return scn;
}

int main(int argc, char **argv)
{
	if (argc < 3) {
		fprintf(stderr,
			"Usage: %s [-s section-suffix] <out.o> <hex32> [<hex32> ...]\n"
			"\n"
			"  -s <suffix>   override section name suffix (default: mvdp.test)\n"
			"                The section is named .text.merlin.<suffix>\n"
			"                Use 'cb.test' for callback-body fixtures.\n",
			argv[0]);
		return 2;
	}

	/* Parse optional -s flag. */
	const char *section_suffix = "mvdp.test";
	int arg_start = 1;
	if (argc >= 3 && strcmp(argv[1], "-s") == 0) {
		section_suffix = argv[2];
		arg_start = 3;
	}

	if (arg_start + 1 > argc) {
		fprintf(stderr, "Usage: %s [-s suffix] <out.o> <hex32>...\n",
			argv[0]);
		return 2;
	}

	const char *out_path = argv[arg_start];
	size_t n = (size_t)(argc - arg_start - 1);

	/* Encode the instruction stream into a flat LE byte buffer. */
	uint8_t *text = calloc(n ? n : 1, 4);
	if (!text) die("oom");
	for (size_t i = 0; i < n; i++) {
		uint32_t w = parse_hex32(argv[arg_start + 1 + i]);
		text[4*i + 0] = (uint8_t)(w      );
		text[4*i + 1] = (uint8_t)(w >>  8);
		text[4*i + 2] = (uint8_t)(w >> 16);
		text[4*i + 3] = (uint8_t)(w >> 24);
	}

	if (elf_version(EV_CURRENT) == EV_NONE)
		die("libelf out of date");

	int fd = open(out_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
	if (fd < 0) die("open %s: %s", out_path, strerror(errno));

	Elf *e = elf_begin(fd, ELF_C_WRITE, NULL);
	if (!e) die("elf_begin: %s", elf_errmsg(-1));

	if (gelf_newehdr(e, ELFCLASS64) == NULL)
		die("gelf_newehdr: %s", elf_errmsg(-1));

	GElf_Ehdr eh;
	if (!gelf_getehdr(e, &eh)) die("gelf_getehdr: %s", elf_errmsg(-1));
	eh.e_ident[EI_DATA]    = ELFDATA2LSB;
	eh.e_ident[EI_OSABI]   = ELFOSABI_NONE;
	eh.e_type              = ET_REL;
	eh.e_machine           = EM_RISCV;
	eh.e_version           = EV_CURRENT;
	if (!gelf_update_ehdr(e, &eh))
		die("gelf_update_ehdr: %s", elf_errmsg(-1));

	/* Section header string table - we manage its bytes manually so
	 * we can append names as we add sections.
	 */
	char *strtab = calloc(1, 1);   /* leading NUL */
	strtab[0] = '\0';
	struct {
		void  *d_buf;
		size_t d_size;
	} strtab_owner = { strtab, 1 };

	Elf_Data strtab_data;
	memset(&strtab_data, 0, sizeof(strtab_data));
	strtab_data.d_buf  = strtab_owner.d_buf;
	strtab_data.d_size = strtab_owner.d_size;

	/* .merlin.meta */
	struct merlin_meta_v1 m;
	memset(&m, 0, sizeof(m));
	m.magic            = MERLIN_META_MAGIC;
	m.version_major    = MERLIN_META_VERSION_MAJOR;
	m.version_minor    = MERLIN_META_VERSION_MINOR;
	m.meta_size        = sizeof(m);
	m.bytecode_profile = MERLIN_PROFILE_LINUX_RV64;
	strncpy(m.prog_name, "test", sizeof(m.prog_name));
	strncpy(m.toolchain, "mkfixture", sizeof(m.toolchain));

	/* .merlin.license */
	const char lic[] = "GPL";

	add_section(e, ".merlin.meta",    &strtab_data, SHT_PROGBITS, SHF_ALLOC, &m, sizeof(m));
	add_section(e, ".merlin.license", &strtab_data, SHT_PROGBITS, SHF_ALLOC, lic, sizeof(lic));
	{
		char sname[128];
		snprintf(sname, sizeof(sname), ".text.merlin.%s", section_suffix);
		add_section(e, sname, &strtab_data,
			    SHT_PROGBITS, SHF_ALLOC | SHF_EXECINSTR, text, n * 4);
	}

	/* .shstrtab itself */
	Elf_Scn *scn = elf_newscn(e);
	if (!scn) die("elf_newscn shstr: %s", elf_errmsg(-1));
	Elf_Data *d = elf_newdata(scn);
	if (!d)   die("elf_newdata shstr: %s", elf_errmsg(-1));

	/* append ".shstrtab" name */
	const char *shstr_name = ".shstrtab";
	size_t shstr_name_off = strtab_data.d_size;
	size_t new_sz = strtab_data.d_size + strlen(shstr_name) + 1;
	char *ns = realloc(strtab_data.d_buf, new_sz);
	if (!ns) die("oom shstr");
	memcpy(ns + strtab_data.d_size, shstr_name, strlen(shstr_name) + 1);
	strtab_data.d_buf  = ns;
	strtab_data.d_size = new_sz;

	d->d_buf     = strtab_data.d_buf;
	d->d_size    = strtab_data.d_size;
	d->d_align   = 1;
	d->d_off     = 0;
	d->d_type    = ELF_T_BYTE;
	d->d_version = EV_CURRENT;

	GElf_Shdr sh;
	if (!gelf_getshdr(scn, &sh)) die("gelf_getshdr shstr: %s", elf_errmsg(-1));
	sh.sh_type    = SHT_STRTAB;
	sh.sh_name    = (uint32_t)shstr_name_off;
	sh.sh_size    = strtab_data.d_size;
	sh.sh_entsize = 0;
	if (!gelf_update_shdr(scn, &sh)) die("gelf_update_shdr shstr: %s", elf_errmsg(-1));

	/* set e_shstrndx */
	size_t shstr_ndx = elf_ndxscn(scn);
	if (!gelf_getehdr(e, &eh)) die("gelf_getehdr 2: %s", elf_errmsg(-1));
	eh.e_shstrndx = (uint16_t)shstr_ndx;
	if (!gelf_update_ehdr(e, &eh)) die("gelf_update_ehdr 2: %s", elf_errmsg(-1));

	elf_flagelf(e, ELF_C_SET, ELF_F_DIRTY);
	if (elf_update(e, ELF_C_WRITE) < 0)
		die("elf_update: %s", elf_errmsg(-1));

	elf_end(e);
	close(fd);
	free(text);

	return 0;
}
