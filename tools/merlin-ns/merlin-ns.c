// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * merlin-ns - MVCP namespace prototype CLI.
 *
 * Subcommands:
 *
 *   merlin-ns create --name <s> [--from <parent.ns>] \
 *                    [--attach LIST] [--helpers LIST] [--kfuncs LIST] \
 *                    [--max-progs N] [--max-maps N] \
 *                    [--max-map-mem BYTES] [--max-prog-mem BYTES] \
 *                    [--inherit] [--seal] -o <out.ns>
 *
 *   merlin-ns dump <file.ns>
 *
 *   merlin-ns check --ns <file.ns> [--usage progs:N,maps:M,...] \
 *                   --attach T --helpers LIST [--kfuncs LIST] \
 *                   [--map-mem BYTES] [--prog-mem BYTES]
 *
 *   merlin-ns compose --child <c.ns> --parent <p.ns> -o <eff.ns>
 *
 * LIST is comma-separated decimal-or-hex IDs (e.g. "0x201,0x110,1").
 */

#include <errno.h>
#include <getopt.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ns.h"

static int parse_list(const char *s, uint32_t *out, size_t cap, size_t *n)
{
	*n = 0;
	if (!s || !*s) return 0;
	const char *p = s;
	while (*p) {
		while (*p == ',' || *p == ' ') p++;
		if (!*p) break;
		char *end;
		unsigned long v = strtoul(p, &end, 0);
		if (end == p) return -1;
		if (*n >= cap) return -1;
		out[(*n)++] = (uint32_t)v;
		p = end;
	}
	return 0;
}

static void set_bits(uint64_t *bs, unsigned nwords,
		     const uint32_t *ids, size_t n)
{
	for (size_t i = 0; i < n; i++)
		merlin_ns_bit_set(bs, ids[i], nwords);
}

static int cmd_create(int argc, char **argv)
{
	struct merlin_ns_config_v1 cfg = {
		.magic = MERLIN_NS_MAGIC,
		.size  = sizeof(struct merlin_ns_config_v1),
		.version = MERLIN_NS_VERSION,
	};
	const char *out_path = NULL;
	const char *parent_path = NULL;
	const char *name = "<unnamed>";
	uint32_t att[64]; size_t att_n = 0;
	uint32_t hlp[256]; size_t hlp_n = 0;
	uint32_t kf [256]; size_t kf_n  = 0;
	bool seal = false;
	bool inherit = false;

	enum { O_NAME=1000, O_FROM, O_AT, O_HL, O_KF,
	       O_MP, O_MM, O_MMM, O_MPM, O_SEAL, O_INH };
	static struct option opts[] = {
		{"name",         required_argument, 0, O_NAME},
		{"from",         required_argument, 0, O_FROM},
		{"attach",       required_argument, 0, O_AT},
		{"helpers",      required_argument, 0, O_HL},
		{"kfuncs",       required_argument, 0, O_KF},
		{"max-progs",    required_argument, 0, O_MP},
		{"max-maps",     required_argument, 0, O_MM},
		{"max-map-mem",  required_argument, 0, O_MMM},
		{"max-prog-mem", required_argument, 0, O_MPM},
		{"inherit",      no_argument,       0, O_INH},
		{"seal",         no_argument,       0, O_SEAL},
		{0,0,0,0}
	};
	int c, idx; optind = 2;
	while ((c = getopt_long(argc, argv, "o:", opts, &idx)) != -1) {
		switch (c) {
		case O_NAME: name = optarg; break;
		case O_FROM: parent_path = optarg; break;
		case O_AT:
			if (parse_list(optarg, att, 64, &att_n) < 0) return 2;
			break;
		case O_HL:
			if (parse_list(optarg, hlp, 256, &hlp_n) < 0) return 2;
			break;
		case O_KF:
			if (parse_list(optarg, kf, 256, &kf_n) < 0) return 2;
			break;
		case O_MP:  cfg.max_progs               = strtoull(optarg, NULL, 0); break;
		case O_MM:  cfg.max_maps                = strtoull(optarg, NULL, 0); break;
		case O_MMM: cfg.max_map_memory_bytes    = strtoull(optarg, NULL, 0); break;
		case O_MPM: cfg.max_prog_memory_bytes   = strtoull(optarg, NULL, 0); break;
		case O_SEAL: seal = true; break;
		case O_INH:  inherit = true; break;
		case 'o':   out_path = optarg; break;
		default: return 2;
		}
	}
	if (!out_path) {
		fprintf(stderr, "merlin-ns create: -o required\n");
		return 2;
	}

	strncpy(cfg.name, name, sizeof(cfg.name) - 1);
	if (seal)    cfg.flags |= MERLIN_NS_F_SEALED;
	if (inherit) cfg.flags |= MERLIN_NS_F_INHERIT;

	set_bits(cfg.permit_attach, MERLIN_NS_ATTACH_WORDS, att, att_n);
	set_bits(cfg.permit_helper, MERLIN_NS_HELPER_WORDS, hlp, hlp_n);
	set_bits(cfg.permit_kfunc,  MERLIN_NS_KFUNC_WORDS,  kf,  kf_n);

	if (parent_path) {
		struct merlin_ns_config_v1 par;
		if (merlin_ns_load(parent_path, &par) < 0) {
			fprintf(stderr,
				"merlin-ns: cannot load parent %s\n",
				parent_path);
			return 1;
		}
		struct merlin_ns_config_v1 eff;
		if (merlin_ns_compose(&cfg, &par, &eff) < 0)
			return 1;
		cfg = eff;
		cfg.parent_ns_id = par.parent_ns_id ? par.parent_ns_id : 1;
	}

	if (merlin_ns_save(out_path, &cfg) < 0) {
		fprintf(stderr, "merlin-ns: save failed\n");
		return 1;
	}
	printf("wrote %s name='%s' (size=%u bytes)\n",
	       out_path, cfg.name, cfg.size);
	return 0;
}

static int count_bits(const uint64_t *bs, unsigned nwords)
{
	int c = 0;
	for (unsigned i = 0; i < nwords; i++)
		c += __builtin_popcountll(bs[i]);
	return c;
}

static void print_set_ids(const uint64_t *bs, unsigned nwords,
			  unsigned cap, FILE *f)
{
	bool first = true;
	for (unsigned i = 0; i < cap; i++) {
		if (merlin_ns_bit_get(bs, i, nwords)) {
			fprintf(f, "%s0x%x", first ? "" : ",", i);
			first = false;
		}
	}
}

static int cmd_dump(const char *path)
{
	struct merlin_ns_config_v1 cfg;
	if (merlin_ns_load(path, &cfg) < 0) {
		fprintf(stderr, "merlin-ns: load %s failed\n", path);
		return 1;
	}
	printf("namespace '%s' (size=%u)\n", cfg.name, cfg.size);
	printf("  flags                = 0x%x%s%s\n",
	       cfg.flags,
	       (cfg.flags & MERLIN_NS_F_SEALED)  ? " SEALED"  : "",
	       (cfg.flags & MERLIN_NS_F_INHERIT) ? " INHERIT" : "");
	printf("  parent_ns_id         = %u\n", cfg.parent_ns_id);
	printf("  permit_attach (%d): ",
	       count_bits(cfg.permit_attach, MERLIN_NS_ATTACH_WORDS));
	print_set_ids(cfg.permit_attach, MERLIN_NS_ATTACH_WORDS, 128, stdout);
	printf("\n");
	printf("  permit_helper (%d): ",
	       count_bits(cfg.permit_helper, MERLIN_NS_HELPER_WORDS));
	print_set_ids(cfg.permit_helper, MERLIN_NS_HELPER_WORDS, 4096, stdout);
	printf("\n");
	printf("  permit_kfunc  (%d): ",
	       count_bits(cfg.permit_kfunc, MERLIN_NS_KFUNC_WORDS));
	print_set_ids(cfg.permit_kfunc, MERLIN_NS_KFUNC_WORDS, 4096, stdout);
	printf("\n");
	printf("  max_progs            = %lu\n", (unsigned long)cfg.max_progs);
	printf("  max_maps             = %lu\n", (unsigned long)cfg.max_maps);
	printf("  max_map_memory_bytes = %lu\n", (unsigned long)cfg.max_map_memory_bytes);
	printf("  max_prog_memory_bytes= %lu\n", (unsigned long)cfg.max_prog_memory_bytes);
	return 0;
}

static int parse_usage(const char *s, struct merlin_ns_usage *u)
{
	memset(u, 0, sizeof(*u));
	if (!s) return 0;
	const char *p = s;
	while (*p) {
		while (*p == ',' || *p == ' ') p++;
		if (!*p) break;
		const char *colon = strchr(p, ':');
		if (!colon) return -1;
		char key[32];
		size_t kl = (size_t)(colon - p);
		if (kl >= sizeof(key)) return -1;
		memcpy(key, p, kl);
		key[kl] = '\0';
		const char *vstart = colon + 1;
		char *end;
		unsigned long long v = strtoull(vstart, &end, 0);
		if      (!strcmp(key, "progs"))    u->live_progs = v;
		else if (!strcmp(key, "maps"))     u->live_maps  = v;
		else if (!strcmp(key, "map_mem"))  u->live_map_memory_bytes = v;
		else if (!strcmp(key, "prog_mem")) u->live_prog_memory_bytes = v;
		else { fprintf(stderr, "unknown usage key '%s'\n", key); return -1; }
		p = end;
	}
	return 0;
}

static int cmd_check(int argc, char **argv)
{
	const char *ns_path = NULL;
	const char *usage_s = NULL;
	struct merlin_prog_facts prog = {0};

	enum { O_NS=2000, O_AT, O_HL, O_KF, O_USAGE, O_MAP_MEM, O_PROG_MEM };
	static struct option opts[] = {
		{"ns",       required_argument, 0, O_NS},
		{"attach",   required_argument, 0, O_AT},
		{"helpers",  required_argument, 0, O_HL},
		{"kfuncs",   required_argument, 0, O_KF},
		{"usage",    required_argument, 0, O_USAGE},
		{"map-mem",  required_argument, 0, O_MAP_MEM},
		{"prog-mem", required_argument, 0, O_PROG_MEM},
		{0,0,0,0}
	};
	int c, idx; optind = 2;
	size_t hn = 0, kn = 0;
	while ((c = getopt_long(argc, argv, "", opts, &idx)) != -1) {
		switch (c) {
		case O_NS:    ns_path = optarg; break;
		case O_AT:    prog.attach_type = (uint32_t)strtoul(optarg, NULL, 0); break;
		case O_HL:
			if (parse_list(optarg, prog.helper_ids, 256, &hn) < 0)
				return 2;
			prog.helper_id_count = (uint32_t)hn;
			break;
		case O_KF:
			if (parse_list(optarg, prog.kfunc_ids, 256, &kn) < 0)
				return 2;
			prog.kfunc_id_count = (uint32_t)kn;
			break;
		case O_USAGE:    usage_s = optarg; break;
		case O_MAP_MEM:  prog.map_memory_bytes  = strtoull(optarg, NULL, 0); break;
		case O_PROG_MEM: prog.prog_memory_bytes = strtoull(optarg, NULL, 0); break;
		default: return 2;
		}
	}
	if (!ns_path) {
		fprintf(stderr, "merlin-ns check: --ns required\n");
		return 2;
	}
	struct merlin_ns_config_v1 cfg;
	if (merlin_ns_load(ns_path, &cfg) < 0) {
		fprintf(stderr, "merlin-ns: load %s failed\n", ns_path);
		return 1;
	}
	struct merlin_ns_usage usage;
	if (parse_usage(usage_s, &usage) < 0) return 2;

	uint32_t bad = 0;
	enum merlin_ns_reject r = merlin_ns_check(&cfg, &usage, &prog, &bad);
	if (r == MERLIN_NS_OK) {
		printf("ACCEPT (ns='%s')\n", cfg.name);
		return 0;
	}
	printf("REJECT  reason=%s  offending_id=0x%x\n",
	       merlin_ns_reject_name(r), bad);
	return 1;
}

static int cmd_compose(int argc, char **argv)
{
	const char *child = NULL, *parent = NULL, *out = NULL;
	enum { O_CHILD = 3000, O_PARENT, O_OUT };
	static struct option opts[] = {
		{"child",  required_argument, 0, O_CHILD},
		{"parent", required_argument, 0, O_PARENT},
		{0,0,0,0}
	};
	int c, idx; optind = 2;
	while ((c = getopt_long(argc, argv, "o:", opts, &idx)) != -1) {
		switch (c) {
		case O_CHILD:  child = optarg;  break;
		case O_PARENT: parent = optarg; break;
		case 'o':      out = optarg;    break;
		default: return 2;
		}
	}
	if (!child || !parent || !out) {
		fprintf(stderr,
			"merlin-ns compose: --child --parent -o required\n");
		return 2;
	}
	struct merlin_ns_config_v1 c1, p1, eff;
	if (merlin_ns_load(child,  &c1) < 0) return 1;
	if (merlin_ns_load(parent, &p1) < 0) return 1;
	if (merlin_ns_compose(&c1, &p1, &eff) < 0) return 1;
	if (merlin_ns_save(out, &eff) < 0) return 1;
	printf("composed -> %s\n", out);
	return 0;
}

static void usage_help(void)
{
	fprintf(stderr,
		"Usage:\n"
		"  merlin-ns create --name <s> [--from <parent.ns>]\n"
		"                   [--attach LIST] [--helpers LIST] [--kfuncs LIST]\n"
		"                   [--max-progs N] [--max-maps N]\n"
		"                   [--max-map-mem N] [--max-prog-mem N]\n"
		"                   [--inherit] [--seal]  -o <out.ns>\n"
		"  merlin-ns dump <file.ns>\n"
		"  merlin-ns check --ns <file.ns> --attach T --helpers LIST\n"
		"                  [--kfuncs LIST] [--usage progs:N,maps:M,...]\n"
		"                  [--map-mem N] [--prog-mem N]\n"
		"  merlin-ns compose --child <c.ns> --parent <p.ns> -o <eff.ns>\n");
}

int main(int argc, char **argv)
{
	if (argc < 2) { usage_help(); return 2; }
	if (strcmp(argv[1], "create")  == 0) return cmd_create(argc, argv);
	if (strcmp(argv[1], "dump")    == 0 && argc == 3) return cmd_dump(argv[2]);
	if (strcmp(argv[1], "check")   == 0) return cmd_check(argc, argv);
	if (strcmp(argv[1], "compose") == 0) return cmd_compose(argc, argv);
	usage_help();
	return 2;
}
