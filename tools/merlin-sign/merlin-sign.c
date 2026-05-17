// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * merlin-sign - sign and verify MERLIN-V ELF objects.
 *
 * Subcommands:
 *
 *   merlin-sign sign -k <priv.pem> -i <key_id> <in.o> <out.o>
 *       Hash the canonical signed region, sign with ed25519, embed
 *       a .merlin.sig section.  Writes out.o.
 *
 *   merlin-sign verify -k <pub.pem> <in.o>
 *       Extract the .merlin.sig, recompute the hash, verify the
 *       ed25519 signature.  Exit 0 on success, 1 on failure.
 *
 *   merlin-sign tag <in.o>
 *       Print the canonical SHA-256 of the signed region.  Useful
 *       for sanity-checking against merlin-objtool sha256.
 *
 *   merlin-sign dump <in.o>
 *       Print the .merlin.sig header fields if present.
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

#include "sig.h"

static void hex_print(const uint8_t *p, size_t n)
{
	for (size_t i = 0; i < n; i++) printf("%02x", p[i]);
}

static const char *algo_name(uint32_t a)
{
	switch (a) {
	case MERLIN_SIG_ALGO_NONE:         return "none";
	case MERLIN_SIG_ALGO_ED25519:      return "ed25519";
	case MERLIN_SIG_ALGO_ECDSA_P256:   return "ecdsa-p256";
	case MERLIN_SIG_ALGO_RSA_PSS_2048: return "rsa-pss-2048";
	default:                           return "unknown";
	}
}

static int cmd_tag(const char *path)
{
	int fd = open(path, O_RDONLY);
	if (fd < 0) { perror("open"); return 1; }
	uint8_t d[MERLIN_SHA256_LEN];
	uint64_t total = 0;
	if (merlin_sig_hash_signed_region(fd, d, &total) < 0) {
		fprintf(stderr, "merlin-sign: hash failed\n");
		close(fd); return 1;
	}
	close(fd);
	hex_print(d, MERLIN_SHA256_LEN);
	printf("  %s  (signed region: %" PRIu64 " bytes)\n", path, total);
	return 0;
}

static int cmd_sign(const char *priv_path, uint32_t key_id,
		    const char *in_path, const char *out_path)
{
	int fd = open(in_path, O_RDONLY);
	if (fd < 0) { perror("open in"); return 1; }
	uint8_t digest[MERLIN_SHA256_LEN];
	uint64_t total = 0;
	if (merlin_sig_hash_signed_region(fd, digest, &total) < 0) {
		fprintf(stderr, "merlin-sign: hash failed\n");
		close(fd); return 1;
	}
	close(fd);

	uint8_t sig[MERLIN_SIG_ED25519_LEN];
	if (merlin_sig_sign_ed25519(digest, priv_path, sig) < 0) {
		fprintf(stderr,
			"merlin-sign: sign failed (bad private key?)\n");
		return 1;
	}

	struct merlin_sig_v1 hdr = {
		.magic            = MERLIN_SIG_MAGIC,
		.sig_size         = MERLIN_SIG_V1_HEADER_SIZE,
		.algo             = MERLIN_SIG_ALGO_ED25519,
		.key_id           = key_id,
		.signed_blob_off  = 0,             /* canonical-list mode */
		.signed_blob_len  = total,
		.sig_bytes_len    = MERLIN_SIG_ED25519_LEN,
		._reserved        = 0,
	};

	if (merlin_sig_embed(in_path, out_path, &hdr, sig,
			     MERLIN_SIG_ED25519_LEN) < 0) {
		fprintf(stderr, "merlin-sign: embed failed\n");
		return 1;
	}
	printf("signed %s -> %s\n", in_path, out_path);
	printf("  key_id      = 0x%08x\n", key_id);
	printf("  algo        = ed25519\n");
	printf("  hashed      = %" PRIu64 " bytes\n", total);
	printf("  tag         = "); hex_print(digest, MERLIN_SHA256_LEN); printf("\n");
	return 0;
}

static int cmd_verify(const char *pub_path, const char *in_path)
{
	struct merlin_sig_v1 hdr;
	uint8_t sig[MERLIN_SIG_ED25519_LEN];
	bool have = false;

	if (merlin_sig_extract(in_path, &hdr, sig, sizeof(sig), &have) < 0) {
		fprintf(stderr,
			"merlin-sign: extract failed (corrupt .merlin.sig?)\n");
		return 1;
	}
	if (!have) {
		fprintf(stderr,
			"merlin-sign: %s has no .merlin.sig section\n",
			in_path);
		return 1;
	}
	if (hdr.algo != MERLIN_SIG_ALGO_ED25519) {
		fprintf(stderr,
			"merlin-sign: algo %u not supported (v0 = ed25519 only)\n",
			hdr.algo);
		return 1;
	}

	int fd = open(in_path, O_RDONLY);
	if (fd < 0) { perror("open"); return 1; }
	uint8_t digest[MERLIN_SHA256_LEN];
	uint64_t total = 0;
	if (merlin_sig_hash_signed_region(fd, digest, &total) < 0) {
		fprintf(stderr, "merlin-sign: hash failed\n");
		close(fd); return 1;
	}
	close(fd);

	if (total != hdr.signed_blob_len) {
		fprintf(stderr,
			"merlin-sign: hashed-bytes %" PRIu64
			" != .merlin.sig signed_blob_len %" PRIu64 "\n",
			total, hdr.signed_blob_len);
		return 1;
	}

	if (merlin_sig_verify_ed25519(digest, sig, pub_path) < 0) {
		fprintf(stderr, "merlin-sign: VERIFY FAILED\n");
		return 1;
	}
	printf("VERIFY OK\n");
	printf("  key_id      = 0x%08x\n", hdr.key_id);
	printf("  algo        = %s\n",     algo_name(hdr.algo));
	printf("  hashed      = %" PRIu64 " bytes\n", total);
	printf("  tag         = "); hex_print(digest, MERLIN_SHA256_LEN); printf("\n");
	return 0;
}

static int cmd_dump(const char *in_path)
{
	struct merlin_sig_v1 hdr;
	uint8_t sig[MERLIN_SIG_ED25519_LEN];
	bool have = false;
	if (merlin_sig_extract(in_path, &hdr, sig, sizeof(sig), &have) < 0) {
		fprintf(stderr,
			"merlin-sign: extract failed\n");
		return 1;
	}
	if (!have) {
		printf("%s: no .merlin.sig\n", in_path);
		return 0;
	}
	printf(".merlin.sig:\n");
	printf("  magic            = 0x%08x\n", hdr.magic);
	printf("  sig_size         = %u\n", hdr.sig_size);
	printf("  algo             = %u (%s)\n",
	       hdr.algo, algo_name(hdr.algo));
	printf("  key_id           = 0x%08x\n", hdr.key_id);
	printf("  signed_blob_off  = %" PRIu64 "\n", hdr.signed_blob_off);
	printf("  signed_blob_len  = %" PRIu64 "\n", hdr.signed_blob_len);
	printf("  sig_bytes_len    = %u\n", hdr.sig_bytes_len);
	printf("  sig              = ");
	hex_print(sig, hdr.sig_bytes_len);
	printf("\n");
	return 0;
}

static void usage(void)
{
	fprintf(stderr,
		"Usage:\n"
		"  merlin-sign sign -k <priv.pem> -i <key_id> <in.o> <out.o>\n"
		"  merlin-sign verify -k <pub.pem> <in.o>\n"
		"  merlin-sign tag <in.o>\n"
		"  merlin-sign dump <in.o>\n");
}

int main(int argc, char **argv)
{
	if (argc < 2) { usage(); return 2; }

	const char *cmd = argv[1];

	if (strcmp(cmd, "tag") == 0) {
		if (argc != 3) { usage(); return 2; }
		return cmd_tag(argv[2]);
	}

	if (strcmp(cmd, "dump") == 0) {
		if (argc != 3) { usage(); return 2; }
		return cmd_dump(argv[2]);
	}

	if (strcmp(cmd, "verify") == 0) {
		const char *pub = NULL;
		int opt;
		optind = 2;
		while ((opt = getopt(argc, argv, "k:")) != -1) {
			if (opt == 'k') pub = optarg;
			else { usage(); return 2; }
		}
		if (!pub || optind >= argc) { usage(); return 2; }
		return cmd_verify(pub, argv[optind]);
	}

	if (strcmp(cmd, "sign") == 0) {
		const char *priv = NULL;
		uint32_t key_id = 0;
		bool key_id_set = false;
		int opt;
		optind = 2;
		while ((opt = getopt(argc, argv, "k:i:")) != -1) {
			switch (opt) {
			case 'k': priv = optarg; break;
			case 'i':
				key_id = (uint32_t)strtoul(optarg, NULL, 0);
				key_id_set = true;
				break;
			default: usage(); return 2;
			}
		}
		if (!priv || !key_id_set || optind + 1 >= argc) {
			usage(); return 2;
		}
		return cmd_sign(priv, key_id, argv[optind], argv[optind + 1]);
	}

	usage();
	return 2;
}
