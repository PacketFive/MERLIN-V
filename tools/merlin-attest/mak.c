// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * mak.c - MAK lifecycle and quote sign/verify (ed25519).
 */
#include "mak.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <openssl/evp.h>
#include <openssl/pem.h>

char merlin_mak_dir[256] = "/tmp/merlin-mak";

static const char *mak_dir(void)
{
	const char *env = getenv("MERLIN_MAK_DIR");
	if (env && env[0]) {
		strncpy(merlin_mak_dir, env, sizeof(merlin_mak_dir) - 1);
		merlin_mak_dir[sizeof(merlin_mak_dir) - 1] = '\0';
	}
	return merlin_mak_dir;
}

static int mak_path(const char *suffix, char *out, size_t cap)
{
	const char *d = mak_dir();
	int n = snprintf(out, cap, "%s/mak.%s", d, suffix);
	return (n < 0 || (size_t)n >= cap) ? -1 : 0;
}

static int ensure_dir(const char *d)
{
	struct stat st;
	if (stat(d, &st) == 0)
		return S_ISDIR(st.st_mode) ? 0 : -1;
	if (mkdir(d, 0700) < 0) {
		if (errno == EEXIST) return 0;
		return -1;
	}
	return 0;
}

int merlin_mak_ensure(void)
{
	const char *d = mak_dir();
	if (ensure_dir(d) < 0)
		return -1;

	char priv_path[320], pub_path[320];
	if (mak_path("priv.pem", priv_path, sizeof(priv_path))) return -1;
	if (mak_path("pub.pem",  pub_path,  sizeof(pub_path)))  return -1;

	struct stat st;
	if (stat(priv_path, &st) == 0 && stat(pub_path, &st) == 0)
		return 0;  /* already present */

	EVP_PKEY_CTX *kctx = EVP_PKEY_CTX_new_id(EVP_PKEY_ED25519, NULL);
	if (!kctx) return -1;
	EVP_PKEY *pk = NULL;
	int rc = -1;
	if (EVP_PKEY_keygen_init(kctx) != 1)         goto out;
	if (EVP_PKEY_keygen(kctx, &pk) != 1)         goto out;

	FILE *fpriv = fopen(priv_path, "w");
	if (!fpriv) goto out;
	if (PEM_write_PrivateKey(fpriv, pk, NULL, NULL, 0, NULL, NULL) != 1) {
		fclose(fpriv); goto out;
	}
	fclose(fpriv);
	chmod(priv_path, 0600);

	FILE *fpub = fopen(pub_path, "w");
	if (!fpub) goto out;
	if (PEM_write_PUBKEY(fpub, pk) != 1) { fclose(fpub); goto out; }
	fclose(fpub);
	chmod(pub_path, 0644);

	rc = 0;
out:
	EVP_PKEY_free(pk);
	EVP_PKEY_CTX_free(kctx);
	return rc;
}

int merlin_mak_pub_hash(uint8_t out[MERLIN_ATTEST_KEYHASH_LEN])
{
	char pub_path[320];
	if (mak_path("pub.pem", pub_path, sizeof(pub_path))) return -1;
	FILE *f = fopen(pub_path, "r");
	if (!f) return -1;
	EVP_PKEY *pk = PEM_read_PUBKEY(f, NULL, NULL, NULL);
	fclose(f);
	if (!pk) return -1;

	uint8_t *der = NULL;
	int dlen = i2d_PUBKEY(pk, &der);
	EVP_PKEY_free(pk);
	if (dlen <= 0 || !der) return -1;

	unsigned int olen = MERLIN_ATTEST_KEYHASH_LEN;
	EVP_MD_CTX *ctx = EVP_MD_CTX_new();
	int rc = -1;
	if (!ctx) goto out;
	if (EVP_DigestInit_ex(ctx, EVP_sha256(), NULL) != 1) goto out;
	if (EVP_DigestUpdate(ctx, der, dlen) != 1) goto out;
	if (EVP_DigestFinal_ex(ctx, out, &olen) != 1) goto out;
	rc = 0;
out:
	OPENSSL_free(der);
	EVP_MD_CTX_free(ctx);
	return rc;
}

uint64_t merlin_mak_seq_read(void)
{
	char p[320];
	if (mak_path("seq", p, sizeof(p))) return 0;
	FILE *f = fopen(p, "r");
	if (!f) return 0;
	uint64_t v = 0;
	if (fscanf(f, "%lu", &v) != 1) v = 0;
	fclose(f);
	return v;
}

int merlin_mak_seq_write(uint64_t v)
{
	char p[320];
	if (mak_path("seq", p, sizeof(p))) return -1;
	FILE *f = fopen(p, "w");
	if (!f) return -1;
	fprintf(f, "%lu\n", v);
	fclose(f);
	return 0;
}

static uint64_t boot_ns(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_BOOTTIME, &ts);
	return (uint64_t)ts.tv_sec * 1000000000ull + ts.tv_nsec;
}

#define ED25519_SIG_LEN 64

int merlin_mak_sign_quote(
	const struct merlin_attestation_v1 *subject,
	const uint8_t nonce[MERLIN_ATTEST_NONCE_LEN],
	uint8_t *out_quote, size_t out_cap, size_t *out_len)
{
	if (out_cap < sizeof(struct merlin_attestation_v1) + ED25519_SIG_LEN)
		return -1;

	struct merlin_attestation_v1 *q =
		(struct merlin_attestation_v1 *)out_quote;
	memcpy(q, subject, sizeof(*q));

	q->magic    = MERLIN_ATTESTATION_MAGIC;
	q->size     = (uint32_t)sizeof(*q);
	q->version  = MERLIN_ATTESTATION_VERSION;
	q->algo     = MERLIN_ATTEST_ALGO_ED25519;
	q->attesting_key_algo = MERLIN_ATTEST_ALGO_ED25519;

	q->attestation_time_ns_boot = boot_ns();
	q->quote_seq = merlin_mak_seq_read() + 1;
	memcpy(q->nonce, nonce, MERLIN_ATTEST_NONCE_LEN);
	q->sig_offset = (uint32_t)sizeof(*q);
	q->sig_len    = ED25519_SIG_LEN;

	if (merlin_mak_pub_hash(q->attesting_key_pub_hash) < 0)
		return -1;

	/* HW chain absent in v0 prototype. */
	q->hw_chain_present = 0;
	q->hw_chain_kind    = MERLIN_HW_CHAIN_NONE;
	q->hw_chain_offset  = 0;
	q->hw_chain_len     = 0;
	memset(q->hw_chain_digest, 0, sizeof(q->hw_chain_digest));

	/* Sign [0, sig_offset) with the MAK private key. */
	char priv_path[320];
	if (mak_path("priv.pem", priv_path, sizeof(priv_path))) return -1;
	FILE *f = fopen(priv_path, "r");
	if (!f) return -1;
	EVP_PKEY *pk = PEM_read_PrivateKey(f, NULL, NULL, NULL);
	fclose(f);
	if (!pk) return -1;

	EVP_MD_CTX *ctx = EVP_MD_CTX_new();
	int rc = -1;
	if (!ctx) { EVP_PKEY_free(pk); return -1; }
	size_t sl = ED25519_SIG_LEN;
	if (EVP_DigestSignInit(ctx, NULL, NULL, NULL, pk) != 1) goto out;
	if (EVP_DigestSign(ctx,
			   out_quote + q->sig_offset, &sl,
			   out_quote, q->sig_offset) != 1)         goto out;
	if (sl != ED25519_SIG_LEN) goto out;

	/* Commit the new seq.  In the kernel implementation this is an
	 * atomic-write that the signing primitive performs before
	 * releasing the signed quote; in user space we order it after
	 * a successful sign. */
	merlin_mak_seq_write(q->quote_seq);

	*out_len = sizeof(*q) + ED25519_SIG_LEN;
	rc = 0;
out:
	EVP_MD_CTX_free(ctx);
	EVP_PKEY_free(pk);
	return rc;
}

int merlin_mak_verify_quote(
	const uint8_t *quote, size_t quote_len,
	const char *pub_pem_path,
	const uint8_t expected_nonce[MERLIN_ATTEST_NONCE_LEN],
	uint64_t *expected_last_seq_p)
{
	if (quote_len < sizeof(struct merlin_attestation_v1))
		return -1;

	const struct merlin_attestation_v1 *q =
		(const struct merlin_attestation_v1 *)quote;

	if (q->magic   != MERLIN_ATTESTATION_MAGIC)             return -1;
	if (q->version != MERLIN_ATTESTATION_VERSION)           return -1;
	if (q->algo    != MERLIN_ATTEST_ALGO_ED25519)           return -1;
	if (q->size    != sizeof(*q))                           return -1;
	if (q->sig_offset != sizeof(*q))                        return -1;
	if (q->sig_len    != ED25519_SIG_LEN)                   return -1;
	if (quote_len < (size_t)q->sig_offset + q->sig_len)     return -1;

	/* Nonce check (constant-time-ish via memcmp; OK for v0). */
	if (memcmp(q->nonce, expected_nonce,
		   MERLIN_ATTEST_NONCE_LEN) != 0)
		return -1;

	/* Replay defence: seq must be strictly greater than the
	 * highest we've seen for this MAK. */
	if (expected_last_seq_p && q->quote_seq <= *expected_last_seq_p)
		return -1;

	/* Cryptographic verify */
	FILE *f = fopen(pub_pem_path, "r");
	if (!f) return -1;
	EVP_PKEY *pk = PEM_read_PUBKEY(f, NULL, NULL, NULL);
	fclose(f);
	if (!pk) return -1;

	EVP_MD_CTX *ctx = EVP_MD_CTX_new();
	int rc = -1;
	if (!ctx) { EVP_PKEY_free(pk); return -1; }
	if (EVP_DigestVerifyInit(ctx, NULL, NULL, NULL, pk) != 1) goto out;
	if (EVP_DigestVerify(ctx,
			     quote + q->sig_offset, q->sig_len,
			     quote, q->sig_offset) != 1)        goto out;
	rc = 0;
	if (expected_last_seq_p)
		*expected_last_seq_p = q->quote_seq;
out:
	EVP_MD_CTX_free(ctx);
	EVP_PKEY_free(pk);
	return rc;
}
