/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * sig.h - MERLIN-V .merlin.sig wire format and helpers.
 *
 * Canonical spec: docs/design/09-mvcp-kernel-uapi.md §3.1.
 *
 * The signature is computed over the SHA-256 of the canonical
 * signed region: the concatenation, in section-name-ordered
 * sequence, of every section the spec lists as part of the signed
 * region.  This is the same byte stream merlin-objtool's sha256
 * subcommand emits, so a program's "tag" (the digest) is identical
 * regardless of which tool produced it.
 *
 * The signed_blob_off / signed_blob_len fields of struct
 * merlin_sig_v1 are informational under this scheme: signed_blob_off
 * is set to 0 (sentinel "see canonical section list"), and
 * signed_blob_len records the total byte length of the hashed
 * region.  The fields exist for forward compatibility with a future
 * single-region encoding (e.g. an out-of-line attestation blob).
 */
#ifndef MERLIN_SIGN_SIG_H
#define MERLIN_SIGN_SIG_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define MERLIN_SIG_MAGIC    0x47495356u   /* 'VSIG' little-endian */

enum merlin_sig_algo {
	MERLIN_SIG_ALGO_NONE         = 0,
	MERLIN_SIG_ALGO_ED25519      = 1,
	MERLIN_SIG_ALGO_ECDSA_P256   = 2,
	MERLIN_SIG_ALGO_RSA_PSS_2048 = 3,
};

struct merlin_sig_v1 {
	uint32_t magic;            /* MERLIN_SIG_MAGIC                       */
	uint32_t sig_size;         /* sizeof(this struct) for forward compat */
	uint32_t algo;             /* enum merlin_sig_algo                   */
	uint32_t key_id;           /* opaque key identifier; matches keyring */
	uint64_t signed_blob_off;  /* 0 = canonical-section-list mode        */
	uint64_t signed_blob_len;  /* bytes hashed                           */
	uint32_t sig_bytes_len;    /* length of signature trailing bytes     */
	uint32_t _reserved;        /* MBZ                                    */
	/* signature bytes follow, sig_bytes_len long */
} __attribute__((packed));

#define MERLIN_SIG_V1_HEADER_SIZE  ((uint32_t)sizeof(struct merlin_sig_v1))
#define MERLIN_SIG_ED25519_LEN     64u
#define MERLIN_SHA256_LEN          32u

/*
 * Canonical list of signed sections.  Order matters: the loader and
 * the signing tool walk this list identically.  ".text.merlin.*"
 * sections come last and are appended in the order they appear in
 * the section header table.
 */
extern const char *const merlin_sig_canonical_fixed_sections[];
extern const size_t       merlin_sig_canonical_fixed_count;

/*
 * Compute the SHA-256 digest of the canonical signed region of an
 * ELF object given as a memory image.  Returns 0 on success.
 *
 * elf_fd:        readable fd of the ELF; mmap'd internally.
 * digest_out:    receives a MERLIN_SHA256_LEN-byte digest.
 * total_bytes:   if non-NULL, receives the total bytes hashed.
 */
int merlin_sig_hash_signed_region(int elf_fd,
				  uint8_t digest_out[MERLIN_SHA256_LEN],
				  uint64_t *total_bytes);

/*
 * Sign / verify (ed25519 only in v0).
 */
int merlin_sig_sign_ed25519(const uint8_t digest[MERLIN_SHA256_LEN],
			    const char *priv_key_pem_path,
			    uint8_t sig_out[MERLIN_SIG_ED25519_LEN]);

int merlin_sig_verify_ed25519(const uint8_t digest[MERLIN_SHA256_LEN],
			      const uint8_t sig[MERLIN_SIG_ED25519_LEN],
			      const char *pub_key_pem_path);

/*
 * Embed / extract the .merlin.sig section.
 *
 * merlin_sig_embed: read in_path, append .merlin.sig containing
 *   header+sig_bytes, write to out_path.  Any existing .merlin.sig
 *   in the input is replaced.
 *
 * merlin_sig_extract: locate .merlin.sig in path; populate header
 *   and sig_bytes (up to sig_bytes_cap).  *have_sig=false if absent.
 */
int merlin_sig_embed(const char *in_path,
		     const char *out_path,
		     const struct merlin_sig_v1 *header,
		     const uint8_t *sig_bytes,
		     uint32_t sig_bytes_len);

int merlin_sig_extract(const char *path,
		       struct merlin_sig_v1 *header_out,
		       uint8_t *sig_bytes_out,
		       size_t sig_bytes_cap,
		       bool *have_sig_out);

#endif /* MERLIN_SIGN_SIG_H */
