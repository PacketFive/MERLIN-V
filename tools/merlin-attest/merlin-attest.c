// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * merlin-attest - MVCP attestation CLI.
 *
 * Subcommands:
 *
 *   merlin-attest init
 *       Create the MAK if missing.  Idempotent.
 *
 *   merlin-attest pub
 *       Print the MAK public key path.
 *
 *   merlin-attest quote --prog-id N --prog-tag <hex> \
 *                       [--ns-id M] [--profile P] \
 *                       [--load-time-ns T] \
 *                       --nonce <hex> -o <out.bin>
 *       Sign a quote covering (prog_id, ns_id, profile, prog_tag,
 *       load_time_ns_boot, nonce, fresh quote_seq).  Writes the
 *       binary quote to <out.bin>.
 *
 *   merlin-attest verify --pub <pub.pem> --nonce <hex> [--last-seq N] <quote.bin>
 *       Verify the quote.  Exits 0 on success, 1 on any failure.
 *
 *   merlin-attest dump <quote.bin>
 *       Pretty-print the quote header fields.
 */

#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "mak.h"

static int hex_to_bytes(const char *s, uint8_t *out, size_t out_len)
{
	if (strlen(s) != out_len * 2) return -1;
	for (size_t i = 0; i < out_len; i++) {
		unsigned v;
		if (sscanf(s + 2*i, "%2x", &v) != 1) return -1;
		out[i] = (uint8_t)v;
	}
	return 0;
}

static void bytes_to_hex(const uint8_t *p, size_t n, FILE *f)
{
	for (size_t i = 0; i < n; i++) fprintf(f, "%02x", p[i]);
}

static int read_all(const char *path, uint8_t **out, size_t *len)
{
	FILE *f = fopen(path, "rb");
	if (!f) return -1;
	fseek(f, 0, SEEK_END);
	long sz = ftell(f);
	if (sz < 0) { fclose(f); return -1; }
	fseek(f, 0, SEEK_SET);
	*out = malloc((size_t)sz);
	if (!*out) { fclose(f); return -1; }
	if (fread(*out, 1, (size_t)sz, f) != (size_t)sz) {
		free(*out); fclose(f); return -1;
	}
	fclose(f);
	*len = (size_t)sz;
	return 0;
}

static const char *algo_name(uint32_t a)
{
	switch (a) {
	case MERLIN_ATTEST_ALGO_NONE:         return "none";
	case MERLIN_ATTEST_ALGO_ED25519:      return "ed25519";
	case MERLIN_ATTEST_ALGO_ECDSA_P256:   return "ecdsa-p256";
	case MERLIN_ATTEST_ALGO_RSA_PSS_2048: return "rsa-pss-2048";
	default:                              return "?";
	}
}

static const char *hw_name(uint32_t k)
{
	switch (k) {
	case MERLIN_HW_CHAIN_NONE:         return "none";
	case MERLIN_HW_CHAIN_TPM_2_0:      return "tpm2";
	case MERLIN_HW_CHAIN_ARM_CCA:      return "cca";
	case MERLIN_HW_CHAIN_INTEL_TDX:    return "tdx";
	case MERLIN_HW_CHAIN_AMD_SEV_SNP:  return "sev-snp";
	case MERLIN_HW_CHAIN_KEYSTONE_TEE: return "keystone";
	case MERLIN_HW_CHAIN_RISCV_AIA:    return "riscv-aia";
	default:                           return "?";
	}
}

static int cmd_init(void)
{
	if (merlin_mak_ensure() < 0) {
		fprintf(stderr, "merlin-attest: MAK init failed\n");
		return 1;
	}
	printf("MAK ready in %s\n", merlin_mak_dir);
	uint8_t hash[MERLIN_ATTEST_KEYHASH_LEN];
	if (merlin_mak_pub_hash(hash) == 0) {
		printf("MAK pub-hash: ");
		bytes_to_hex(hash, sizeof(hash), stdout);
		printf("\n");
	}
	printf("MAK seq:      %lu\n",
	       (unsigned long)merlin_mak_seq_read());
	return 0;
}

static int cmd_pub(void)
{
	merlin_mak_ensure();
	printf("%s/mak.pub.pem\n", merlin_mak_dir);
	return 0;
}

static int cmd_quote(int argc, char **argv)
{
	uint32_t prog_id = 0, ns_id = 0, profile = 0;
	uint64_t load_time = 0;
	uint8_t  prog_tag[MERLIN_ATTEST_TAG_LEN] = {0};
	uint8_t  nonce[MERLIN_ATTEST_NONCE_LEN]  = {0};
	bool     have_tag = false, have_nonce = false;
	const char *out = NULL;

	enum { O_PROGID = 1000, O_NSID, O_PROFILE, O_TAG, O_NONCE,
	       O_LOAD, O_OUT };
	static struct option opts[] = {
		{"prog-id",       required_argument, 0, O_PROGID},
		{"ns-id",         required_argument, 0, O_NSID},
		{"profile",       required_argument, 0, O_PROFILE},
		{"prog-tag",      required_argument, 0, O_TAG},
		{"nonce",         required_argument, 0, O_NONCE},
		{"load-time-ns",  required_argument, 0, O_LOAD},
		{"out",           required_argument, 0, 'o'},
		{0,0,0,0}
	};
	int idx, c;
	optind = 2;
	while ((c = getopt_long(argc, argv, "o:", opts, &idx)) != -1) {
		switch (c) {
		case O_PROGID:  prog_id  = (uint32_t)strtoul(optarg, NULL, 0); break;
		case O_NSID:    ns_id    = (uint32_t)strtoul(optarg, NULL, 0); break;
		case O_PROFILE: profile  = (uint32_t)strtoul(optarg, NULL, 0); break;
		case O_LOAD:    load_time= (uint64_t)strtoull(optarg, NULL, 0); break;
		case O_TAG:
			if (hex_to_bytes(optarg, prog_tag, sizeof(prog_tag)) < 0) {
				fprintf(stderr,
					"merlin-attest: bad --prog-tag (need 64 hex chars)\n");
				return 2;
			}
			have_tag = true;
			break;
		case O_NONCE:
			if (hex_to_bytes(optarg, nonce, sizeof(nonce)) < 0) {
				fprintf(stderr,
					"merlin-attest: bad --nonce (need 64 hex chars)\n");
				return 2;
			}
			have_nonce = true;
			break;
		case 'o': case O_OUT: out = optarg; break;
		default: return 2;
		}
	}
	if (!have_tag || !have_nonce || !out) {
		fprintf(stderr,
			"merlin-attest quote: --prog-tag, --nonce, -o required\n");
		return 2;
	}
	if (merlin_mak_ensure() < 0) {
		fprintf(stderr, "merlin-attest: MAK init failed\n");
		return 1;
	}

	struct merlin_attestation_v1 subj = {0};
	subj.prog_id            = prog_id;
	subj.ns_id              = ns_id;
	subj.profile            = profile;
	subj.load_time_ns_boot  = load_time;
	subj.attesting_key_id   = 0;
	memcpy(subj.prog_tag, prog_tag, sizeof(subj.prog_tag));

	uint8_t quote[sizeof(struct merlin_attestation_v1) + 64];
	size_t  qlen = 0;
	if (merlin_mak_sign_quote(&subj, nonce, quote, sizeof(quote),
				  &qlen) < 0) {
		fprintf(stderr, "merlin-attest: sign failed\n");
		return 1;
	}

	FILE *f = fopen(out, "wb");
	if (!f) { perror("fopen"); return 1; }
	fwrite(quote, 1, qlen, f);
	fclose(f);

	printf("wrote %zu bytes to %s\n", qlen, out);
	const struct merlin_attestation_v1 *q =
		(const struct merlin_attestation_v1 *)quote;
	printf("  prog_id      = %u\n", q->prog_id);
	printf("  ns_id        = %u\n", q->ns_id);
	printf("  prog_tag     = "); bytes_to_hex(q->prog_tag, sizeof(q->prog_tag), stdout); printf("\n");
	printf("  quote_seq    = %lu\n", (unsigned long)q->quote_seq);
	return 0;
}

static int cmd_verify(int argc, char **argv)
{
	const char *pub = NULL;
	uint8_t nonce[MERLIN_ATTEST_NONCE_LEN] = {0};
	bool have_nonce = false;
	uint64_t last_seq = 0;
	bool seq_provided = false;
	int idx, c;
	enum { O_PUB = 1500, O_NONCE_V, O_LASTSEQ };
	static struct option opts[] = {
		{"pub",       required_argument, 0, O_PUB},
		{"nonce",     required_argument, 0, O_NONCE_V},
		{"last-seq",  required_argument, 0, O_LASTSEQ},
		{0,0,0,0}
	};
	optind = 2;
	while ((c = getopt_long(argc, argv, "", opts, &idx)) != -1) {
		switch (c) {
		case O_PUB: pub = optarg; break;
		case O_NONCE_V:
			if (hex_to_bytes(optarg, nonce, sizeof(nonce)) < 0) {
				fprintf(stderr, "merlin-attest: bad --nonce\n");
				return 2;
			}
			have_nonce = true;
			break;
		case O_LASTSEQ:
			last_seq = (uint64_t)strtoull(optarg, NULL, 0);
			seq_provided = true;
			break;
		default: return 2;
		}
	}
	if (!pub || !have_nonce || optind >= argc) {
		fprintf(stderr,
			"merlin-attest verify: --pub, --nonce, <quote.bin> required\n");
		return 2;
	}

	uint8_t *q; size_t qlen;
	if (read_all(argv[optind], &q, &qlen) < 0) {
		fprintf(stderr, "merlin-attest: cannot read %s\n", argv[optind]);
		return 1;
	}

	uint64_t *seq_p = seq_provided ? &last_seq : NULL;
	int rc = merlin_mak_verify_quote(q, qlen, pub, nonce, seq_p);
	free(q);
	if (rc != 0) {
		fprintf(stderr, "VERIFY FAILED\n");
		return 1;
	}
	printf("VERIFY OK\n");
	if (seq_provided)
		printf("  new last-seq = %lu\n", (unsigned long)last_seq);
	return 0;
}

static int cmd_dump(const char *path)
{
	uint8_t *q; size_t qlen;
	if (read_all(path, &q, &qlen) < 0) {
		fprintf(stderr, "cannot read %s\n", path);
		return 1;
	}
	if (qlen < sizeof(struct merlin_attestation_v1)) {
		fprintf(stderr, "too short\n"); free(q); return 1;
	}
	const struct merlin_attestation_v1 *a =
		(const struct merlin_attestation_v1 *)q;
	printf(".merlin-attestation v%u (%u bytes header, %u + %u sig):\n",
	       a->version, a->size, a->sig_offset, a->sig_len);
	printf("  magic           = 0x%08x\n", a->magic);
	printf("  algo            = %s\n",     algo_name(a->algo));
	printf("  prog_id         = %u\n",     a->prog_id);
	printf("  ns_id           = %u\n",     a->ns_id);
	printf("  profile         = %u\n",     a->profile);
	printf("  prog_tag        = "); bytes_to_hex(a->prog_tag, sizeof(a->prog_tag), stdout); printf("\n");
	printf("  load_time_ns    = %" PRIu64 "\n", a->load_time_ns_boot);
	printf("  attest_time_ns  = %" PRIu64 "\n", a->attestation_time_ns_boot);
	printf("  quote_seq       = %" PRIu64 "\n", a->quote_seq);
	printf("  attest_key_id   = %u\n", a->attesting_key_id);
	printf("  attest_key_algo = %s\n", algo_name(a->attesting_key_algo));
	printf("  pub_hash        = "); bytes_to_hex(a->attesting_key_pub_hash, sizeof(a->attesting_key_pub_hash), stdout); printf("\n");
	printf("  hw_chain        = %s present=%u\n",
	       hw_name(a->hw_chain_kind), a->hw_chain_present);
	printf("  nonce           = "); bytes_to_hex(a->nonce, sizeof(a->nonce), stdout); printf("\n");
	free(q);
	return 0;
}

static void usage(void)
{
	fprintf(stderr,
		"Usage:\n"
		"  merlin-attest init\n"
		"  merlin-attest pub\n"
		"  merlin-attest quote --prog-id N --prog-tag <hex64> [--ns-id M]\n"
		"                      [--profile P] [--load-time-ns T]\n"
		"                      --nonce <hex64> -o <out.bin>\n"
		"  merlin-attest verify --pub <pub.pem> --nonce <hex64>\n"
		"                       [--last-seq N] <quote.bin>\n"
		"  merlin-attest dump <quote.bin>\n");
}

int main(int argc, char **argv)
{
	if (argc < 2) { usage(); return 2; }
	if (strcmp(argv[1], "init")   == 0) return cmd_init();
	if (strcmp(argv[1], "pub")    == 0) return cmd_pub();
	if (strcmp(argv[1], "quote")  == 0) return cmd_quote(argc, argv);
	if (strcmp(argv[1], "verify") == 0) return cmd_verify(argc, argv);
	if (strcmp(argv[1], "dump")   == 0 && argc == 3) return cmd_dump(argv[2]);
	usage();
	return 2;
}
