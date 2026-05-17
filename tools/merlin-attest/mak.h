/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * mak.h - MERLIN Attestation Key (MAK) lifecycle + quote signing/verifying.
 *
 * Implements the v0 of docs/design/11-mvcp-attestation.md §3 in
 * user space.  The MAK is an ed25519 keypair held on disk under
 * /var/lib/merlin/mak.{priv,pub}.pem (path configurable); a small
 * companion file /var/lib/merlin/mak.seq holds the next quote
 * sequence number to defend against replay.
 *
 * In the eventual kernel implementation the MAK lives in the
 * kernel keyring, persisted optionally via a TPM NV index;
 * quote_seq is a per-MAK atomic counter.  The user-space prototype
 * uses files so the algorithm is fully exercisable without
 * privileged hardware.
 */
#ifndef MERLIN_ATTEST_MAK_H
#define MERLIN_ATTEST_MAK_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "merlin/attestation.h"

/* MAK file paths.  Override via env MERLIN_MAK_DIR. */
extern char merlin_mak_dir[256];

/*
 * Ensure a MAK exists; generate ed25519 keypair if not.
 * Returns 0 on success.  Idempotent.
 */
int merlin_mak_ensure(void);

/*
 * Compute SHA-256 of the MAK public key DER and return it.
 * out must hold MERLIN_ATTEST_KEYHASH_LEN bytes.
 */
int merlin_mak_pub_hash(uint8_t out[MERLIN_ATTEST_KEYHASH_LEN]);

/*
 * Read/write the per-MAK monotonic sequence file.  Caller is
 * responsible for sequencing concurrent updates (the user-space
 * prototype is single-threaded; the kernel version uses an
 * atomic counter).
 */
uint64_t merlin_mak_seq_read(void);
int      merlin_mak_seq_write(uint64_t v);

/*
 * Build and sign a quote.
 *
 * subject:    the "what is being attested" half of the quote
 *             (caller fills prog_id, ns_id, profile, prog_tag,
 *             load_time_ns_boot).
 * nonce:      32 bytes, controller-supplied.
 * out_quote:  buffer of at least sizeof(merlin_attestation_v1) +
 *             64 (ed25519 sig) bytes.  Receives the signed quote.
 * out_len:    receives the total bytes written.
 *
 * Returns 0 on success.
 */
int merlin_mak_sign_quote(
	const struct merlin_attestation_v1 *subject,
	const uint8_t nonce[MERLIN_ATTEST_NONCE_LEN],
	uint8_t *out_quote, size_t out_cap, size_t *out_len);

/*
 * Verify a quote.
 *
 * pub_pem_path:   path to the MAK public key PEM file (verifier-supplied).
 * expected_nonce: 32 bytes; must match quote.nonce.
 * expected_last_seq_p: optional in/out; if non-NULL, the value
 *                 pointed to is the highest quote_seq the verifier
 *                 has seen.  The function rejects quotes with seq
 *                 <= *p, and updates *p on success.
 *
 * Returns 0 on success; negative on any verification failure with
 * no information leakage about which check failed.
 */
int merlin_mak_verify_quote(
	const uint8_t *quote, size_t quote_len,
	const char *pub_pem_path,
	const uint8_t expected_nonce[MERLIN_ATTEST_NONCE_LEN],
	uint64_t *expected_last_seq_p);

#endif /* MERLIN_ATTEST_MAK_H */
