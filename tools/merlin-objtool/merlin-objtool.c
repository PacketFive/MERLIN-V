// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * merlin-objtool - prototype object-file tool for MERLIN-V programs.
 *
 * This is the FIRST CODE prototype of MERLIN-V.  It exercises the
 * canonical ELF object layout pinned in
 * docs/design/02-isa-and-bytecode.md and the program-side headers
 * under docs/design/uapi/merlin/.
 *
 * Supported subcommands (this prototype):
 *
 *     merlin-objtool info     <object.o>
 *     merlin-objtool dump-meta <object.o>
 *     merlin-objtool dump-maps <object.o>
 *     merlin-objtool dump-relocs <object.o>
 *     merlin-objtool sha256   <object.o>       # hash the signed region
 *     merlin-objtool validate <object.o>       # all-pass static checks
 *
 * Not yet implemented (future increments):
 *
 *     - CO-RE-V marker resolution (.merlin.core_v_pending -> .merlin.relocs
 *       + .merlin.btf_ext post-processing)
 *     - libmerlin loader entry points
 *     - signed-region patching
 *     - CO-RE-V cross-resolution against a target BTF
 *
 * Dependencies: libelf-dev, libcrypto (for SHA-256).
 *
 * Build:  see Makefile in this directory.
 */

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>

#include <gelf.h>
#include <libelf.h>
#include <openssl/evp.h>
#include <openssl/sha.h>      /* SHA256_DIGEST_LENGTH */

/* The wire format constants we depend on.  In the eventual in-tree
 * build these come from <merlin/merlin.h>; for the prototype we
 * include the .h directly off the design tree.
 */
#include "merlin/merlin.h"

/* -------------------------------------------------------------------------
 * Error printing
 * ------------------------------------------------------------------------- */
#define err(fmt, ...) fprintf(stderr, "merlin-objtool: " fmt "\n", ##__VA_ARGS__)
#define die(fmt, ...) do { err(fmt, ##__VA_ARGS__); exit(1); } while (0)

/* -------------------------------------------------------------------------
 * ELF handle bundle
 * ------------------------------------------------------------------------- */
struct elf_obj {
	int      fd;
	Elf     *elf;
	GElf_Ehdr ehdr;
	size_t   shstrndx;
	void    *mapped;
	size_t   mapped_size;
};

static void elf_obj_close(struct elf_obj *o)
{
	if (o->elf)
		elf_end(o->elf);
	if (o->fd >= 0)
		close(o->fd);
}

static int elf_obj_open(const char *path, struct elf_obj *o)
{
	memset(o, 0, sizeof(*o));
	o->fd = -1;

	if (elf_version(EV_CURRENT) == EV_NONE) {
		err("libelf out of date");
		return -1;
	}

	o->fd = open(path, O_RDONLY);
	if (o->fd < 0) {
		err("open %s: %s", path, strerror(errno));
		return -1;
	}

	struct stat st;
	if (fstat(o->fd, &st) < 0) {
		err("fstat: %s", strerror(errno));
		goto fail;
	}
	o->mapped_size = (size_t)st.st_size;

	o->elf = elf_begin(o->fd, ELF_C_READ, NULL);
	if (!o->elf) {
		err("elf_begin: %s", elf_errmsg(-1));
		goto fail;
	}

	if (elf_kind(o->elf) != ELF_K_ELF) {
		err("%s is not an ELF object", path);
		goto fail;
	}

	if (!gelf_getehdr(o->elf, &o->ehdr)) {
		err("gelf_getehdr: %s", elf_errmsg(-1));
		goto fail;
	}

	if (elf_getshdrstrndx(o->elf, &o->shstrndx)) {
		err("elf_getshdrstrndx: %s", elf_errmsg(-1));
		goto fail;
	}

	return 0;
fail:
	elf_obj_close(o);
	return -1;
}

/* -------------------------------------------------------------------------
 * Section lookup
 * ------------------------------------------------------------------------- */
static Elf_Scn *find_section(const struct elf_obj *o, const char *name,
			     GElf_Shdr *shdr_out)
{
	Elf_Scn *scn = NULL;

	while ((scn = elf_nextscn(o->elf, scn)) != NULL) {
		GElf_Shdr shdr;

		if (!gelf_getshdr(scn, &shdr))
			continue;

		const char *sname = elf_strptr(o->elf, o->shstrndx, shdr.sh_name);
		if (!sname)
			continue;

		if (strcmp(sname, name) == 0) {
			if (shdr_out)
				*shdr_out = shdr;
			return scn;
		}
	}
	return NULL;
}

static Elf_Data *section_data(Elf_Scn *scn)
{
	return scn ? elf_getdata(scn, NULL) : NULL;
}

/* -------------------------------------------------------------------------
 * Validation
 * ------------------------------------------------------------------------- */
struct validate_ctx {
	bool has_meta;
	bool has_license;
	bool has_maps;
	bool has_relocs;
	bool has_btf;
	bool has_btf_ext;
	bool has_core_v_pending;
	bool has_sig;

	uint32_t profile;
	uint32_t prog_type;
	uint16_t version_major;
	uint16_t version_minor;
	uint32_t meta_size_advertised;
	uint32_t meta_section_size;
	size_t   map_count;
	size_t   reloc_count;
};

static int validate_meta(const struct elf_obj *o, struct validate_ctx *ctx,
			 bool verbose)
{
	GElf_Shdr shdr;
	Elf_Scn *scn = find_section(o, ".merlin.meta", &shdr);
	if (!scn) {
		if (verbose)
			err("missing required section .merlin.meta");
		return -1;
	}
	ctx->has_meta = true;
	ctx->meta_section_size = (uint32_t)shdr.sh_size;

	Elf_Data *d = section_data(scn);
	if (!d || d->d_size < 12) {
		err(".merlin.meta too small");
		return -1;
	}

	const struct merlin_meta_v1 *m = d->d_buf;

	if (m->magic != MERLIN_META_MAGIC) {
		err(".merlin.meta: bad magic 0x%08x (expect 0x%08x)",
		    m->magic, MERLIN_META_MAGIC);
		return -1;
	}
	if (m->version_major != MERLIN_META_VERSION_MAJOR) {
		err(".merlin.meta: unsupported version_major %u",
		    m->version_major);
		return -1;
	}
	if (m->meta_size > d->d_size) {
		err(".merlin.meta: meta_size %u exceeds section size %zu",
		    m->meta_size, d->d_size);
		return -1;
	}
	if (m->meta_size < 12) {
		err(".merlin.meta: meta_size %u below minimum (12)",
		    m->meta_size);
		return -1;
	}

	ctx->profile              = m->bytecode_profile;
	ctx->prog_type            = m->prog_type;
	ctx->version_major        = m->version_major;
	ctx->version_minor        = m->version_minor;
	ctx->meta_size_advertised = m->meta_size;
	return 0;
}

static int validate_license(const struct elf_obj *o, struct validate_ctx *ctx,
			    bool verbose)
{
	Elf_Scn *scn = find_section(o, ".merlin.license", NULL);
	if (!scn) {
		if (verbose)
			err("missing required section .merlin.license");
		return -1;
	}
	ctx->has_license = true;

	Elf_Data *d = section_data(scn);
	if (!d || d->d_size < 1) {
		err(".merlin.license empty");
		return -1;
	}

	/* must be NUL-terminated */
	const char *s = d->d_buf;
	if (memchr(s, 0, d->d_size) == NULL) {
		err(".merlin.license not NUL-terminated");
		return -1;
	}
	return 0;
}

static int validate_maps(const struct elf_obj *o, struct validate_ctx *ctx,
			 bool verbose)
{
	GElf_Shdr shdr;
	Elf_Scn *scn = find_section(o, ".merlin.maps", &shdr);
	if (!scn) {
		ctx->map_count = 0;
		return 0;     /* optional */
	}
	ctx->has_maps = true;

	if (shdr.sh_size % sizeof(struct merlin_map_desc_v1) != 0) {
		err(".merlin.maps size %lu is not a multiple of %zu",
		    (unsigned long)shdr.sh_size,
		    sizeof(struct merlin_map_desc_v1));
		return -1;
	}
	ctx->map_count = shdr.sh_size / sizeof(struct merlin_map_desc_v1);

	Elf_Data *d = section_data(scn);
	if (!d)
		return -1;

	const struct merlin_map_desc_v1 *descs = d->d_buf;
	for (size_t i = 0; i < ctx->map_count; i++) {
		if (descs[i].type == 0) {
			err(".merlin.maps[%zu]: type MERLIN_MAP_TYPE_UNSPEC",
			    i);
			return -1;
		}
		if (descs[i].max_entries == 0
		    && descs[i].type != MERLIN_MAP_TYPE_RINGBUF) {
			err(".merlin.maps[%zu]: max_entries=0 for non-ringbuf",
			    i);
			return -1;
		}
		(void)verbose;
	}
	return 0;
}

static int validate_relocs(const struct elf_obj *o, struct validate_ctx *ctx,
			   bool verbose)
{
	struct merlin_reloc_v1 {
		uint32_t r_offset;
		uint32_t r_type;
		uint32_t r_sym;
		int32_t  r_addend;
		uint32_t r_extra0;
		uint32_t r_extra1;
	};

	GElf_Shdr shdr;
	Elf_Scn *scn = find_section(o, ".merlin.relocs", &shdr);
	if (!scn) {
		ctx->reloc_count = 0;
		return 0;     /* optional */
	}
	ctx->has_relocs = true;

	if (shdr.sh_size % sizeof(struct merlin_reloc_v1) != 0) {
		err(".merlin.relocs size %lu is not a multiple of %zu",
		    (unsigned long)shdr.sh_size,
		    sizeof(struct merlin_reloc_v1));
		return -1;
	}
	ctx->reloc_count = shdr.sh_size / sizeof(struct merlin_reloc_v1);

	Elf_Data *d = section_data(scn);
	if (!d || d->d_size == 0)
		return 0;

	const struct merlin_reloc_v1 *r = d->d_buf;
	uint32_t prev_off = 0;
	bool first = true;
	for (size_t i = 0; i < ctx->reloc_count; i++) {
		if (r[i].r_type == 0) {
			err(".merlin.relocs[%zu]: R_MERLIN_NONE encoded",
			    i);
			return -1;
		}
		/* Reserved/unknown reloc types: default-deny per
		 * 02-isa-and-bytecode.md §8.6.1.
		 */
		if (r[i].r_type > 8 && r[i].r_type < 64) {
			err(".merlin.relocs[%zu]: reserved r_type %u",
			    i, r[i].r_type);
			return -1;
		}
		if (!first && r[i].r_offset <= prev_off) {
			err(".merlin.relocs[%zu]: not sorted (offset %u <= prev %u)",
			    i, r[i].r_offset, prev_off);
			return -1;
		}
		prev_off = r[i].r_offset;
		first = false;
		(void)verbose;
	}
	return 0;
}

static int run_validate(const struct elf_obj *o, struct validate_ctx *ctx,
			bool verbose, bool strict_machine)
{
	/* ELF e_ident / e_machine checks (spec §8.2) */
	if (o->ehdr.e_ident[EI_DATA] != ELFDATA2LSB) {
		err("ELF is not little-endian (MERLIN-V wire format is LE)");
		return -1;
	}
	if (strict_machine
	    && o->ehdr.e_machine != EM_RISCV
	    && o->ehdr.e_machine != EM_NONE) {
		err("e_machine = %u; expected EM_RISCV (243)",
		    o->ehdr.e_machine);
		return -1;
	}
	if (!strict_machine && verbose
	    && o->ehdr.e_machine != EM_RISCV
	    && o->ehdr.e_machine != EM_NONE) {
		printf("note: e_machine = %u (not EM_RISCV); this looks like "
		       "an off-target compile-test build.\n",
		       o->ehdr.e_machine);
	}

	if (validate_meta(o, ctx, verbose) < 0)
		return -1;
	if (validate_license(o, ctx, verbose) < 0)
		return -1;
	if (validate_maps(o, ctx, verbose) < 0)
		return -1;
	if (validate_relocs(o, ctx, verbose) < 0)
		return -1;

	/* Optional sections - record presence only */
	ctx->has_btf              = find_section(o, ".merlin.btf",          NULL) != NULL;
	ctx->has_btf_ext          = find_section(o, ".merlin.btf_ext",      NULL) != NULL;
	ctx->has_core_v_pending   = find_section(o, ".merlin.core_v_pending", NULL) != NULL;
	ctx->has_sig              = find_section(o, ".merlin.sig",          NULL) != NULL;

	if (ctx->has_core_v_pending && verbose)
		printf("note: .merlin.core_v_pending present; this object "
		       "needs CO-RE-V resolution\n");

	return 0;
}

/* -------------------------------------------------------------------------
 * Subcommand: info
 * ------------------------------------------------------------------------- */
static const char *profile_name(uint32_t p)
{
	switch (p) {
	case MERLIN_PROFILE_LINUX_RV64: return "merlin-linux-rv64";
	case MERLIN_PROFILE_RTOS_RV32:  return "merlin-rtos-rv32";
	case MERLIN_PROFILE_UNSPEC:     return "unspecified";
	default:                        return "unknown";
	}
}

static int cmd_info(const char *path)
{
	struct elf_obj o;
	if (elf_obj_open(path, &o) < 0)
		return 1;

	struct validate_ctx ctx = {0};
	if (run_validate(&o, &ctx, false, false) < 0) {
		elf_obj_close(&o);
		return 1;
	}

	printf("File:      %s\n", path);
	printf("Profile:   %s (%u)\n", profile_name(ctx.profile), ctx.profile);
	printf("Meta ver:  %u.%u  (advertised meta_size %u; section %u)\n",
	       ctx.version_major, ctx.version_minor,
	       ctx.meta_size_advertised, ctx.meta_section_size);
	printf("Prog type: %u\n", ctx.prog_type);
	printf("Maps:      %zu\n",  ctx.map_count);
	printf("Relocs:    %zu\n",  ctx.reloc_count);
	printf("Sections:  meta=%s license=%s maps=%s relocs=%s btf=%s btf_ext=%s sig=%s core_v_pending=%s\n",
	       ctx.has_meta             ? "y" : "n",
	       ctx.has_license          ? "y" : "n",
	       ctx.has_maps             ? "y" : "n",
	       ctx.has_relocs           ? "y" : "n",
	       ctx.has_btf              ? "y" : "n",
	       ctx.has_btf_ext          ? "y" : "n",
	       ctx.has_sig              ? "y" : "n",
	       ctx.has_core_v_pending   ? "y" : "n");

	elf_obj_close(&o);
	return 0;
}

/* -------------------------------------------------------------------------
 * Subcommand: dump-meta
 * ------------------------------------------------------------------------- */
static int cmd_dump_meta(const char *path)
{
	struct elf_obj o;
	if (elf_obj_open(path, &o) < 0)
		return 1;

	Elf_Scn *scn = find_section(&o, ".merlin.meta", NULL);
	if (!scn) {
		err(".merlin.meta missing");
		elf_obj_close(&o);
		return 1;
	}

	Elf_Data *d = section_data(scn);
	if (!d || d->d_size < sizeof(struct merlin_meta_v1)) {
		err(".merlin.meta truncated");
		elf_obj_close(&o);
		return 1;
	}

	const struct merlin_meta_v1 *m = d->d_buf;
	printf(".merlin.meta:\n");
	printf("  magic                 = 0x%08x\n",       m->magic);
	printf("  version               = %u.%u\n",
	       m->version_major, m->version_minor);
	printf("  meta_size             = %u\n",           m->meta_size);
	printf("  flags                 = 0x%08x\n",       m->flags);
	printf("  bytecode_profile      = %u (%s)\n",
	       m->bytecode_profile, profile_name(m->bytecode_profile));
	printf("  prog_type             = %u\n",           m->prog_type);
	printf("  expected_attach_type  = %u\n",           m->expected_attach_type);
	printf("  requested_stack       = %u\n",           m->requested_stack);
	printf("  requested_steps       = %u\n",           m->requested_steps);
	printf("  prog_name             = \"%.*s\"\n",
	       (int)sizeof(m->prog_name), m->prog_name);
	printf("  toolchain             = \"%.*s\"\n",
	       (int)sizeof(m->toolchain), m->toolchain);

	elf_obj_close(&o);
	return 0;
}

/* -------------------------------------------------------------------------
 * Subcommand: dump-maps
 * ------------------------------------------------------------------------- */
static const char *map_type_name(uint32_t t)
{
	static const char *names[] = {
		"UNSPEC", "HASH", "ARRAY", "PERCPU_HASH", "PERCPU_ARRAY",
		"LRU_HASH", "LPM_TRIE", "RINGBUF", "PROG_ARRAY", "XSKMAP",
		"MVSKMAP",
	};
	return t < (sizeof(names) / sizeof(names[0])) ? names[t] : "UNKNOWN";
}

static int cmd_dump_maps(const char *path)
{
	struct elf_obj o;
	if (elf_obj_open(path, &o) < 0)
		return 1;

	Elf_Scn *scn = find_section(&o, ".merlin.maps", NULL);
	if (!scn) {
		printf("(no maps)\n");
		elf_obj_close(&o);
		return 0;
	}

	Elf_Data *d = section_data(scn);
	size_t n = d->d_size / sizeof(struct merlin_map_desc_v1);
	const struct merlin_map_desc_v1 *descs = d->d_buf;

	for (size_t i = 0; i < n; i++) {
		printf("[%2zu] %-20.*s  type=%s key=%u val=%u max=%u flags=0x%x\n",
		       i,
		       (int)sizeof(descs[i].name), descs[i].name,
		       map_type_name(descs[i].type),
		       descs[i].key_size,
		       descs[i].value_size,
		       descs[i].max_entries,
		       descs[i].flags);
	}

	elf_obj_close(&o);
	return 0;
}

/* -------------------------------------------------------------------------
 * Subcommand: dump-relocs
 * ------------------------------------------------------------------------- */
struct merlin_reloc_v1 {
	uint32_t r_offset;
	uint32_t r_type;
	uint32_t r_sym;
	int32_t  r_addend;
	uint32_t r_extra0;
	uint32_t r_extra1;
};

static const char *reloc_type_name(uint32_t t)
{
	switch (t) {
	case 0: return "R_MERLIN_NONE";
	case 1: return "R_MERLIN_HELPER_ID";
	case 2: return "R_MERLIN_KFUNC_SLOT";
	case 3: return "R_MERLIN_MAP_FD";
	case 4: return "R_MERLIN_MAP_VALUE";
	case 5: return "R_MERLIN_CORE_FIELD";
	case 6: return "R_MERLIN_CORE_SIZE";
	case 7: return "R_MERLIN_CORE_ENUMVAL";
	case 8: return "R_MERLIN_PROG_ENTRY";
	default: return "RESERVED";
	}
}

static int cmd_dump_relocs(const char *path)
{
	struct elf_obj o;
	if (elf_obj_open(path, &o) < 0)
		return 1;

	Elf_Scn *scn = find_section(&o, ".merlin.relocs", NULL);
	if (!scn) {
		printf("(no relocs)\n");
		elf_obj_close(&o);
		return 0;
	}

	Elf_Data *d = section_data(scn);
	size_t n = d->d_size / sizeof(struct merlin_reloc_v1);
	const struct merlin_reloc_v1 *r = d->d_buf;

	printf("  off       type                  sym      addend     extra0     extra1\n");
	for (size_t i = 0; i < n; i++) {
		printf("  %08x  %-20s  %8u  %9d  %9u  %9u\n",
		       r[i].r_offset,
		       reloc_type_name(r[i].r_type),
		       r[i].r_sym,
		       r[i].r_addend,
		       r[i].r_extra0,
		       r[i].r_extra1);
	}

	elf_obj_close(&o);
	return 0;
}

/* -------------------------------------------------------------------------
 * Subcommand: sha256 of the signed region
 * -------------------------------------------------------------------------
 *
 * The signed region per docs/design/02-isa-and-bytecode.md §8.4
 * (and tightened by 09-mvcp-kernel-uapi.md §3.1) covers, in section
 * name order: .merlin.meta, every .text.merlin.*, .merlin.maps,
 * .merlin.relocs, .merlin.btf, .merlin.btf_ext, .merlin.license.
 * .merlin.sig itself, the ELF header tables, and .shstrtab are
 * excluded.
 */
static int sha256_section(EVP_MD_CTX *ctx, const struct elf_obj *o,
			  const char *name)
{
	Elf_Scn *scn = find_section(o, name, NULL);
	if (!scn)
		return 0;
	Elf_Data *d = section_data(scn);
	if (!d || !d->d_buf || d->d_size == 0)
		return 0;
	EVP_DigestUpdate(ctx, d->d_buf, d->d_size);
	return (int)d->d_size;
}

static int cmd_sha256(const char *path)
{
	struct elf_obj o;
	if (elf_obj_open(path, &o) < 0)
		return 1;

	EVP_MD_CTX *ctx = EVP_MD_CTX_new();
	if (!ctx) {
		err("EVP_MD_CTX_new failed");
		elf_obj_close(&o);
		return 1;
	}
	if (EVP_DigestInit_ex(ctx, EVP_sha256(), NULL) != 1) {
		err("EVP_DigestInit_ex failed");
		EVP_MD_CTX_free(ctx);
		elf_obj_close(&o);
		return 1;
	}

	static const char *fixed_sections[] = {
		".merlin.meta",
		".merlin.maps",
		".merlin.relocs",
		".merlin.btf",
		".merlin.btf_ext",
		".merlin.license",
	};

	size_t total = 0;
	for (size_t i = 0; i < sizeof(fixed_sections) / sizeof(fixed_sections[0]); i++) {
		int n = sha256_section(ctx, &o, fixed_sections[i]);
		if (n < 0) {
			EVP_MD_CTX_free(ctx);
			elf_obj_close(&o);
			return 1;
		}
		total += (size_t)n;
	}

	/* every .text.merlin.* section */
	Elf_Scn *scn = NULL;
	while ((scn = elf_nextscn(o.elf, scn)) != NULL) {
		GElf_Shdr shdr;
		if (!gelf_getshdr(scn, &shdr))
			continue;
		const char *sname = elf_strptr(o.elf, o.shstrndx, shdr.sh_name);
		if (!sname || strncmp(sname, ".text.merlin.", 13) != 0)
			continue;
		Elf_Data *d = elf_getdata(scn, NULL);
		if (d && d->d_buf && d->d_size) {
			EVP_DigestUpdate(ctx, d->d_buf, d->d_size);
			total += d->d_size;
		}
	}

	uint8_t digest[SHA256_DIGEST_LENGTH];
	unsigned int digest_len = sizeof(digest);
	if (EVP_DigestFinal_ex(ctx, digest, &digest_len) != 1) {
		err("EVP_DigestFinal_ex failed");
		EVP_MD_CTX_free(ctx);
		elf_obj_close(&o);
		return 1;
	}
	EVP_MD_CTX_free(ctx);

	for (size_t i = 0; i < digest_len; i++)
		printf("%02x", digest[i]);
	printf("  %s  (signed region: %zu bytes)\n", path, total);

	elf_obj_close(&o);
	return 0;
}

/* -------------------------------------------------------------------------
 * Subcommand: validate
 * ------------------------------------------------------------------------- */
static int cmd_validate(const char *path)
{
	struct elf_obj o;
	if (elf_obj_open(path, &o) < 0)
		return 1;

	struct validate_ctx ctx = {0};
	int rc = run_validate(&o, &ctx, true, true);
	if (rc == 0)
		printf("%s: OK\n", path);
	else
		printf("%s: FAIL\n", path);

	elf_obj_close(&o);
	return rc == 0 ? 0 : 1;
}

/* -------------------------------------------------------------------------
 * main
 * ------------------------------------------------------------------------- */
static void usage(void)
{
	fprintf(stderr,
		"Usage:\n"
		"  merlin-objtool info        <object.o>\n"
		"  merlin-objtool dump-meta   <object.o>\n"
		"  merlin-objtool dump-maps   <object.o>\n"
		"  merlin-objtool dump-relocs <object.o>\n"
		"  merlin-objtool sha256      <object.o>\n"
		"  merlin-objtool validate    <object.o>\n");
}

int main(int argc, char **argv)
{
	if (argc < 3) {
		usage();
		return 2;
	}

	const char *cmd  = argv[1];
	const char *path = argv[2];

	if (strcmp(cmd, "info")        == 0) return cmd_info(path);
	if (strcmp(cmd, "dump-meta")   == 0) return cmd_dump_meta(path);
	if (strcmp(cmd, "dump-maps")   == 0) return cmd_dump_maps(path);
	if (strcmp(cmd, "dump-relocs") == 0) return cmd_dump_relocs(path);
	if (strcmp(cmd, "sha256")      == 0) return cmd_sha256(path);
	if (strcmp(cmd, "validate")    == 0) return cmd_validate(path);

	usage();
	return 2;
}
