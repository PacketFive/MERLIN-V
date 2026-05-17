// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * sig.c - hashing, signing, verifying, and embedding for .merlin.sig.
 */
#include "sig.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>

#include <gelf.h>
#include <libelf.h>

#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/sha.h>

/* ------------------------------------------------------------------ */
/* canonical section list */
/* ------------------------------------------------------------------ */

const char *const merlin_sig_canonical_fixed_sections[] = {
	".merlin.meta",
	".merlin.maps",
	".merlin.relocs",
	".merlin.btf",
	".merlin.btf_ext",
	".merlin.license",
};
const size_t merlin_sig_canonical_fixed_count =
	sizeof(merlin_sig_canonical_fixed_sections) /
	sizeof(merlin_sig_canonical_fixed_sections[0]);

/* ------------------------------------------------------------------ */
/* hashing                                                             */
/* ------------------------------------------------------------------ */

static int hash_section(EVP_MD_CTX *ctx, Elf *e, Elf_Scn *scn,
			uint64_t *running_total)
{
	Elf_Data *d = elf_getdata(scn, NULL);
	if (!d || !d->d_buf || d->d_size == 0)
		return 0;
	if (EVP_DigestUpdate(ctx, d->d_buf, d->d_size) != 1)
		return -1;
	*running_total += d->d_size;
	(void)e;
	return 0;
}

static Elf_Scn *find_named(Elf *e, size_t shstrndx, const char *name)
{
	Elf_Scn *scn = NULL;
	while ((scn = elf_nextscn(e, scn)) != NULL) {
		GElf_Shdr sh;
		if (!gelf_getshdr(scn, &sh))
			continue;
		const char *sname = elf_strptr(e, shstrndx, sh.sh_name);
		if (sname && strcmp(sname, name) == 0)
			return scn;
	}
	return NULL;
}

int merlin_sig_hash_signed_region(int elf_fd,
				  uint8_t digest_out[MERLIN_SHA256_LEN],
				  uint64_t *total_bytes_out)
{
	if (elf_version(EV_CURRENT) == EV_NONE)
		return -1;

	Elf *e = elf_begin(elf_fd, ELF_C_READ, NULL);
	if (!e)
		return -1;
	if (elf_kind(e) != ELF_K_ELF) {
		elf_end(e);
		return -1;
	}
	size_t shstrndx;
	if (elf_getshdrstrndx(e, &shstrndx) < 0) {
		elf_end(e);
		return -1;
	}

	EVP_MD_CTX *ctx = EVP_MD_CTX_new();
	if (!ctx) { elf_end(e); return -1; }
	if (EVP_DigestInit_ex(ctx, EVP_sha256(), NULL) != 1) {
		EVP_MD_CTX_free(ctx); elf_end(e); return -1;
	}

	uint64_t total = 0;

	/* fixed sections in canonical order */
	for (size_t i = 0; i < merlin_sig_canonical_fixed_count; i++) {
		Elf_Scn *s = find_named(e, shstrndx,
				merlin_sig_canonical_fixed_sections[i]);
		if (s && hash_section(ctx, e, s, &total) < 0)
			goto err;
	}

	/* every .text.merlin.* in section-header order */
	Elf_Scn *scn = NULL;
	while ((scn = elf_nextscn(e, scn)) != NULL) {
		GElf_Shdr sh;
		if (!gelf_getshdr(scn, &sh))
			continue;
		const char *sname = elf_strptr(e, shstrndx, sh.sh_name);
		if (!sname || strncmp(sname, ".text.merlin.", 13) != 0)
			continue;
		if (hash_section(ctx, e, scn, &total) < 0)
			goto err;
	}

	unsigned int dlen = MERLIN_SHA256_LEN;
	if (EVP_DigestFinal_ex(ctx, digest_out, &dlen) != 1)
		goto err;

	EVP_MD_CTX_free(ctx);
	elf_end(e);
	if (total_bytes_out) *total_bytes_out = total;
	return 0;

err:
	EVP_MD_CTX_free(ctx);
	elf_end(e);
	return -1;
}

/* ------------------------------------------------------------------ */
/* sign / verify (ed25519)                                             */
/* ------------------------------------------------------------------ */

static EVP_PKEY *load_pem_priv(const char *path)
{
	FILE *f = fopen(path, "r");
	if (!f) return NULL;
	EVP_PKEY *k = PEM_read_PrivateKey(f, NULL, NULL, NULL);
	fclose(f);
	return k;
}
static EVP_PKEY *load_pem_pub(const char *path)
{
	FILE *f = fopen(path, "r");
	if (!f) return NULL;
	EVP_PKEY *k = PEM_read_PUBKEY(f, NULL, NULL, NULL);
	fclose(f);
	return k;
}

int merlin_sig_sign_ed25519(const uint8_t digest[MERLIN_SHA256_LEN],
			    const char *priv_key_pem_path,
			    uint8_t sig_out[MERLIN_SIG_ED25519_LEN])
{
	EVP_PKEY *pk = load_pem_priv(priv_key_pem_path);
	if (!pk) return -1;

	EVP_MD_CTX *ctx = EVP_MD_CTX_new();
	if (!ctx) { EVP_PKEY_free(pk); return -1; }

	int rc = -1;
	if (EVP_DigestSignInit(ctx, NULL, NULL, NULL, pk) != 1)
		goto out;

	size_t sl = MERLIN_SIG_ED25519_LEN;
	if (EVP_DigestSign(ctx, sig_out, &sl,
			   digest, MERLIN_SHA256_LEN) != 1)
		goto out;
	if (sl != MERLIN_SIG_ED25519_LEN)
		goto out;
	rc = 0;
out:
	EVP_MD_CTX_free(ctx);
	EVP_PKEY_free(pk);
	return rc;
}

int merlin_sig_verify_ed25519(const uint8_t digest[MERLIN_SHA256_LEN],
			      const uint8_t sig[MERLIN_SIG_ED25519_LEN],
			      const char *pub_key_pem_path)
{
	EVP_PKEY *pk = load_pem_pub(pub_key_pem_path);
	if (!pk) return -1;

	EVP_MD_CTX *ctx = EVP_MD_CTX_new();
	if (!ctx) { EVP_PKEY_free(pk); return -1; }

	int rc = -1;
	if (EVP_DigestVerifyInit(ctx, NULL, NULL, NULL, pk) != 1)
		goto out;
	if (EVP_DigestVerify(ctx, sig, MERLIN_SIG_ED25519_LEN,
			     digest, MERLIN_SHA256_LEN) != 1)
		goto out;
	rc = 0;
out:
	EVP_MD_CTX_free(ctx);
	EVP_PKEY_free(pk);
	return rc;
}

/* ------------------------------------------------------------------ */
/* embed / extract                                                     */
/* ------------------------------------------------------------------ */
/*
 * We embed by rewriting the ELF: copy the input verbatim, strip any
 * pre-existing .merlin.sig, append a fresh one.  libelf with
 * ELF_C_RDWR could do this in place, but emitting a fresh ELF via
 * ELF_C_WRITE is simpler and matches what mkfixture does.
 */

struct sec_in {
	char     *name;
	uint8_t  *buf;
	size_t    size;
	uint32_t  sh_type;
	uint64_t  sh_flags;
};

static int read_all_sections(int fd, struct sec_in **out, size_t *n_out,
			     int *machine_out, int *e_class_out)
{
	if (elf_version(EV_CURRENT) == EV_NONE) return -1;
	Elf *e = elf_begin(fd, ELF_C_READ, NULL);
	if (!e || elf_kind(e) != ELF_K_ELF) {
		if (e) elf_end(e);
		return -1;
	}
	GElf_Ehdr eh;
	if (!gelf_getehdr(e, &eh)) { elf_end(e); return -1; }
	*machine_out = eh.e_machine;
	*e_class_out = eh.e_ident[EI_CLASS];

	size_t shstrndx;
	if (elf_getshdrstrndx(e, &shstrndx) < 0) {
		elf_end(e);
		return -1;
	}

	size_t cap = 16, n = 0;
	struct sec_in *arr = calloc(cap, sizeof(*arr));
	if (!arr) { elf_end(e); return -1; }

	Elf_Scn *scn = NULL;
	while ((scn = elf_nextscn(e, scn)) != NULL) {
		GElf_Shdr sh;
		if (!gelf_getshdr(scn, &sh)) continue;

		const char *name = elf_strptr(e, shstrndx, sh.sh_name);
		if (!name) name = "";

		/* skip section-header string table; we manage it manually */
		if (strcmp(name, ".shstrtab") == 0)
			continue;
		/* drop any existing .merlin.sig - we will append a new one */
		if (strcmp(name, ".merlin.sig") == 0)
			continue;
		/* skip NULL section */
		if (sh.sh_type == SHT_NULL)
			continue;

		Elf_Data *d = elf_getdata(scn, NULL);
		uint8_t  *buf  = NULL;
		size_t    size = 0;
		if (d && d->d_buf && d->d_size) {
			buf = malloc(d->d_size);
			if (!buf) goto err;
			memcpy(buf, d->d_buf, d->d_size);
			size = d->d_size;
		}

		if (n == cap) {
			cap *= 2;
			struct sec_in *nb = realloc(arr, cap * sizeof(*arr));
			if (!nb) { free(buf); goto err; }
			arr = nb;
		}
		arr[n].name     = strdup(name);
		arr[n].buf      = buf;
		arr[n].size     = size;
		arr[n].sh_type  = sh.sh_type;
		arr[n].sh_flags = sh.sh_flags;
		if (!arr[n].name) goto err;
		n++;
	}

	elf_end(e);
	*out = arr;
	*n_out = n;
	return 0;
err:
	for (size_t i = 0; i < n; i++) {
		free(arr[i].name);
		free(arr[i].buf);
	}
	free(arr);
	elf_end(e);
	return -1;
}

int merlin_sig_embed(const char *in_path,
		     const char *out_path,
		     const struct merlin_sig_v1 *header,
		     const uint8_t *sig_bytes,
		     uint32_t sig_bytes_len)
{
	int in_fd = open(in_path, O_RDONLY);
	if (in_fd < 0) return -1;

	struct sec_in *secs = NULL;
	size_t n_secs = 0;
	int machine = 0, e_class = 0;
	int rc = read_all_sections(in_fd, &secs, &n_secs, &machine, &e_class);
	close(in_fd);
	if (rc < 0) return -1;

	int out_fd = open(out_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
	if (out_fd < 0) goto fail;

	Elf *eo = elf_begin(out_fd, ELF_C_WRITE, NULL);
	if (!eo) goto fail_fd;

	if (!gelf_newehdr(eo, e_class)) goto fail_eo;

	GElf_Ehdr oh;
	if (!gelf_getehdr(eo, &oh)) goto fail_eo;
	oh.e_ident[EI_DATA]  = ELFDATA2LSB;
	oh.e_ident[EI_OSABI] = ELFOSABI_NONE;
	oh.e_type    = ET_REL;
	oh.e_machine = machine;
	oh.e_version = EV_CURRENT;
	if (!gelf_update_ehdr(eo, &oh)) goto fail_eo;

	/* shstrtab buffer */
	size_t shsz = 1;
	char  *sht  = calloc(shsz, 1);
	if (!sht) goto fail_eo;

	/* append a section that holds 'name' and returns its sh_name offset */
	#define APPEND_NAME(s) ({                                            \
		size_t _o = shsz;                                            \
		size_t _l = strlen(s) + 1;                                   \
		char *_p = realloc(sht, shsz + _l);                          \
		if (!_p) goto fail_sht;                                      \
		sht = _p;                                                    \
		memcpy(sht + shsz, (s), _l);                                 \
		shsz += _l;                                                  \
		(uint32_t)_o;                                                \
	})

	/* write all original sections */
	for (size_t i = 0; i < n_secs; i++) {
		Elf_Scn *s = elf_newscn(eo);
		if (!s) goto fail_sht;
		Elf_Data *d = elf_newdata(s);
		if (!d) goto fail_sht;
		d->d_buf = secs[i].buf;
		d->d_size = secs[i].size;
		d->d_align = 1;
		d->d_off = 0;
		d->d_type = ELF_T_BYTE;
		d->d_version = EV_CURRENT;
		GElf_Shdr sh;
		if (!gelf_getshdr(s, &sh)) goto fail_sht;
		sh.sh_type    = secs[i].sh_type;
		sh.sh_flags   = secs[i].sh_flags;
		sh.sh_name    = APPEND_NAME(secs[i].name);
		sh.sh_size    = secs[i].size;
		sh.sh_entsize = 0;
		if (!gelf_update_shdr(s, &sh)) goto fail_sht;
	}

	/* append .merlin.sig: header + sig_bytes */
	uint8_t *sig_blob = malloc(MERLIN_SIG_V1_HEADER_SIZE + sig_bytes_len);
	if (!sig_blob) goto fail_sht;
	memcpy(sig_blob, header, MERLIN_SIG_V1_HEADER_SIZE);
	if (sig_bytes_len)
		memcpy(sig_blob + MERLIN_SIG_V1_HEADER_SIZE,
		       sig_bytes, sig_bytes_len);

	{
		Elf_Scn *s = elf_newscn(eo);
		if (!s) { free(sig_blob); goto fail_sht; }
		Elf_Data *d = elf_newdata(s);
		if (!d) { free(sig_blob); goto fail_sht; }
		d->d_buf  = sig_blob;
		d->d_size = MERLIN_SIG_V1_HEADER_SIZE + sig_bytes_len;
		d->d_align = 1;
		d->d_off = 0;
		d->d_type = ELF_T_BYTE;
		d->d_version = EV_CURRENT;
		GElf_Shdr sh;
		if (!gelf_getshdr(s, &sh)) { free(sig_blob); goto fail_sht; }
		sh.sh_type    = SHT_PROGBITS;
		sh.sh_flags   = 0;
		sh.sh_name    = APPEND_NAME(".merlin.sig");
		sh.sh_size    = d->d_size;
		sh.sh_entsize = 0;
		if (!gelf_update_shdr(s, &sh)) { free(sig_blob); goto fail_sht; }
	}

	/* .shstrtab itself */
	{
		uint32_t shstrtab_name_off = APPEND_NAME(".shstrtab");
		Elf_Scn *s = elf_newscn(eo);
		if (!s) goto fail_sht;
		Elf_Data *d = elf_newdata(s);
		if (!d) goto fail_sht;
		d->d_buf = sht;
		d->d_size = shsz;
		d->d_align = 1;
		d->d_off = 0;
		d->d_type = ELF_T_BYTE;
		d->d_version = EV_CURRENT;
		GElf_Shdr sh;
		if (!gelf_getshdr(s, &sh)) goto fail_sht;
		sh.sh_type    = SHT_STRTAB;
		sh.sh_name    = shstrtab_name_off;
		sh.sh_size    = shsz;
		sh.sh_entsize = 0;
		if (!gelf_update_shdr(s, &sh)) goto fail_sht;

		size_t idx = elf_ndxscn(s);
		if (!gelf_getehdr(eo, &oh)) goto fail_sht;
		oh.e_shstrndx = (uint16_t)idx;
		if (!gelf_update_ehdr(eo, &oh)) goto fail_sht;
	}

	elf_flagelf(eo, ELF_C_SET, ELF_F_DIRTY);
	if (elf_update(eo, ELF_C_WRITE) < 0) goto fail_sht;

	elf_end(eo);
	close(out_fd);
	for (size_t i = 0; i < n_secs; i++) {
		free(secs[i].name);
		free(secs[i].buf);
	}
	free(secs);
	return 0;

fail_sht:
	free(sht);
fail_eo:
	elf_end(eo);
fail_fd:
	close(out_fd);
fail:
	for (size_t i = 0; i < n_secs; i++) {
		free(secs[i].name);
		free(secs[i].buf);
	}
	free(secs);
	return -1;
}

int merlin_sig_extract(const char *path,
		       struct merlin_sig_v1 *header_out,
		       uint8_t *sig_bytes_out,
		       size_t sig_bytes_cap,
		       bool *have_sig_out)
{
	*have_sig_out = false;
	if (elf_version(EV_CURRENT) == EV_NONE) return -1;
	int fd = open(path, O_RDONLY);
	if (fd < 0) return -1;
	Elf *e = elf_begin(fd, ELF_C_READ, NULL);
	if (!e || elf_kind(e) != ELF_K_ELF) {
		if (e) elf_end(e);
		close(fd);
		return -1;
	}
	size_t shstrndx;
	if (elf_getshdrstrndx(e, &shstrndx) < 0) goto fail;

	Elf_Scn *scn = find_named(e, shstrndx, ".merlin.sig");
	if (!scn) {
		elf_end(e); close(fd);
		return 0;  /* unsigned but not an error */
	}
	Elf_Data *d = elf_getdata(scn, NULL);
	if (!d || !d->d_buf || d->d_size < MERLIN_SIG_V1_HEADER_SIZE)
		goto fail;

	memcpy(header_out, d->d_buf, MERLIN_SIG_V1_HEADER_SIZE);
	if (header_out->magic != MERLIN_SIG_MAGIC)         goto fail;
	if (header_out->sig_size != MERLIN_SIG_V1_HEADER_SIZE) goto fail;
	if (d->d_size != (size_t)MERLIN_SIG_V1_HEADER_SIZE
			 + header_out->sig_bytes_len)      goto fail;
	if (header_out->sig_bytes_len > sig_bytes_cap)     goto fail;

	memcpy(sig_bytes_out,
	       (const uint8_t *)d->d_buf + MERLIN_SIG_V1_HEADER_SIZE,
	       header_out->sig_bytes_len);

	*have_sig_out = true;
	elf_end(e); close(fd);
	return 0;
fail:
	elf_end(e); close(fd);
	return -1;
}
