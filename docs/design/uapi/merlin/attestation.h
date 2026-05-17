/* SPDX-License-Identifier: GPL-2.0 OR BSD-2-Clause */
/*
 * <merlin/attestation.h> - canonical MVCP attestation quote format.
 *
 * Pinned by docs/design/11-mvcp-attestation.md §2.  This header is
 * the source of truth for the on-wire layout; markdown is rationale.
 *
 * Hash and signature algorithm versioning:
 *
 *   - The fixed header below is signed using `algo` over the
 *     bytes in [0, sig_offset).  The signature bytes occupy
 *     [sig_offset, sig_offset + sig_len).
 *   - sig_offset is set by the producer to sizeof(struct
 *     merlin_attestation_v1) (i.e. immediately after the header).
 *   - Bytes beyond sig_offset + sig_len carry the optional HW
 *     evidence blob, addressed by (hw_chain_offset, hw_chain_len).
 *
 * Replay defence:
 *
 *   - `nonce` MUST be controller-supplied and unpredictable; the
 *     producer echoes it verbatim into the quote.
 *   - `quote_seq` is a monotonically increasing counter per MAK.
 *     A verifier rejects any quote whose seq is <= the highest
 *     seq it has seen from that MAK for the same (host, prog).
 */
#ifndef _MERLIN_ATTESTATION_H
#define _MERLIN_ATTESTATION_H

#include <merlin/types.h>

#define MERLIN_ATTESTATION_MAGIC    0x54534D4Bu   /* 'KMST' little-endian */
#define MERLIN_ATTESTATION_VERSION  1u

/* SHA-256 digest length, in bytes. */
#define MERLIN_ATTEST_DIGEST_LEN    32

/* Length of nonce / prog_tag / key-hash fields. */
#define MERLIN_ATTEST_NONCE_LEN     32
#define MERLIN_ATTEST_TAG_LEN       32
#define MERLIN_ATTEST_KEYHASH_LEN   32

/* Length of kernel build-id (matches /sys/kernel/notes layout). */
#define MERLIN_ATTEST_BUILDID_LEN   20

/* Signature algorithms used inside the quote.  Mirrors the
 * enum merlin_sig_algo values in tools/merlin-sign/sig.h to keep
 * the two in sync.
 */
enum merlin_attest_algo {
	MERLIN_ATTEST_ALGO_NONE         = 0,
	MERLIN_ATTEST_ALGO_ED25519      = 1,
	MERLIN_ATTEST_ALGO_ECDSA_P256   = 2,
	MERLIN_ATTEST_ALGO_RSA_PSS_2048 = 3,
};

/* Hardware-root-of-trust kinds the quote may chain to. */
enum merlin_hw_chain_kind {
	MERLIN_HW_CHAIN_NONE        = 0,
	MERLIN_HW_CHAIN_TPM_2_0     = 1,
	MERLIN_HW_CHAIN_ARM_CCA     = 2,
	MERLIN_HW_CHAIN_INTEL_TDX   = 3,
	MERLIN_HW_CHAIN_AMD_SEV_SNP = 4,
	MERLIN_HW_CHAIN_KEYSTONE_TEE= 5,
	MERLIN_HW_CHAIN_RISCV_AIA   = 6,
};

struct merlin_attestation_v1 {
	/* Header identification */
	__u32 magic;                                     /* MERLIN_ATTESTATION_MAGIC */
	__u32 size;                                      /* sizeof(struct) as emitted */
	__u32 version;                                   /* MERLIN_ATTESTATION_VERSION */
	__u32 algo;                                      /* enum merlin_attest_algo  */

	/* Subject: what is being attested */
	__u32 prog_id;
	__u32 ns_id;
	__u32 profile;                                   /* enum merlin_profile      */
	__u32 _pad0;
	__u8  prog_tag[MERLIN_ATTEST_TAG_LEN];           /* SHA-256 signed region    */

	/* Context */
	__u64 load_time_ns_boot;                         /* CLOCK_BOOTTIME at load    */
	__u64 attestation_time_ns_boot;
	__u64 quote_seq;                                 /* monotonic per-MAK         */
	__u8  kernel_build_id[MERLIN_ATTEST_BUILDID_LEN];/* /sys/kernel/notes         */
	__u8  _pad1[4];

	/* Identity of the key that signed this quote (the MAK) */
	__u32 attesting_key_id;
	__u32 attesting_key_algo;                        /* enum merlin_attest_algo  */
	__u8  attesting_key_pub_hash[MERLIN_ATTEST_KEYHASH_LEN];

	/* Hardware chain (zero-filled if no HW root) */
	__u32 hw_chain_present;                          /* 0 or 1                   */
	__u32 hw_chain_kind;                             /* enum merlin_hw_chain_kind*/
	__u32 hw_chain_offset;                           /* offset from start        */
	__u32 hw_chain_len;
	__u8  hw_chain_digest[MERLIN_ATTEST_DIGEST_LEN]; /* SHA-256 of HW evidence  */

	/* Challenge */
	__u8  nonce[MERLIN_ATTEST_NONCE_LEN];

	/* Signature pointers */
	__u32 sig_offset;                                /* >= sizeof(this struct)   */
	__u32 sig_len;
};

#endif /* _MERLIN_ATTESTATION_H */
