// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * merlin-txn - MVCP batch-transaction prototype CLI.
 *
 * Subcommands (all operate on a single global in-memory store):
 *
 *   merlin-txn script <file.txn>
 *       Execute a text script (one command per line, '#' comments).
 *       Script commands mirror the kernel API one-to-one so tests
 *       read as system-call traces.
 *
 *   Commands available in scripts and on the command line:
 *
 *     map-create                  -> print handle N
 *     map-destroy N
 *     map-dump N
 *     map-lookup N key
 *
 *     txn-begin [ns_id]
 *     txn-stage map_handle op key [value]
 *         op: insert | update | upsert | delete | replace-all
 *     txn-commit [drain|fast]
 *     txn-abort
 *
 *   The test battery drives the tool via shell scripts that call these
 *   commands as subcommands of `merlin-txn`.
 */
#include <errno.h>
#include <getopt.h>
#include <inttypes.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "store.h"

/* State shared across all commands in one invocation. */
static struct merlin_txn  g_txn;
static bool               g_txn_active = false;
static uint32_t           g_last_map   = 0;

static void die(const char *fmt, ...)
{
	va_list ap; va_start(ap, fmt); vfprintf(stderr, fmt, ap); va_end(ap);
	fprintf(stderr, "\n");
	exit(1);
}

static enum merlin_txn_op_type parse_op(const char *s)
{
	if (!strcmp(s, "insert"))      return MERLIN_MAP_TXN_INSERT;
	if (!strcmp(s, "update"))      return MERLIN_MAP_TXN_UPDATE;
	if (!strcmp(s, "upsert"))      return MERLIN_MAP_TXN_UPSERT;
	if (!strcmp(s, "delete"))      return MERLIN_MAP_TXN_DELETE;
	if (!strcmp(s, "replace-all")) return MERLIN_MAP_TXN_REPLACE_ALL;
	die("unknown op '%s'", s);
	return 0;
}

/* Execute a single command line.  Returns 0 or a status string. */
static int exec_cmd(int argc, char **argv)
{
	if (argc < 1) return 0;
	const char *cmd = argv[0];

	if (!strcmp(cmd, "map-create")) {
		uint32_t h = merlin_store_map_create();
		if (!h) die("map-create: no slots left");
		g_last_map = h;
		printf("map-create -> %u\n", h);
		return 0;
	}

	if (!strcmp(cmd, "map-destroy") && argc >= 2) {
		uint32_t h = (uint32_t)strtoul(argv[1], NULL, 0);
		merlin_store_map_destroy(h);
		printf("map-destroy %u\n", h);
		return 0;
	}

	if (!strcmp(cmd, "map-dump") && argc >= 2) {
		uint32_t h = (uint32_t)strtoul(argv[1], NULL, 0);
		printf("map-dump handle=%u count=%u:\n",
		       h, merlin_store_count(h));
		size_t pos = 0;
		uint64_t k, v;
		while (merlin_store_next(h, &pos, &k, &v))
			printf("  %lu -> %lu\n",
			       (unsigned long)k, (unsigned long)v);
		return 0;
	}

	if (!strcmp(cmd, "map-lookup") && argc >= 3) {
		uint32_t h   = (uint32_t)strtoul(argv[1], NULL, 0);
		uint64_t key = (uint64_t)strtoull(argv[2], NULL, 0);
		uint64_t val;
		if (merlin_store_lookup(h, key, &val))
			printf("lookup %lu -> %lu\n",
			       (unsigned long)key, (unsigned long)val);
		else
			printf("lookup %lu -> NOTFOUND\n", (unsigned long)key);
		return 0;
	}

	if (!strcmp(cmd, "txn-begin")) {
		uint32_t ns = argc >= 2 ? (uint32_t)strtoul(argv[1], NULL, 0) : 0;
		if (g_txn_active) die("txn-begin: txn already open");
		merlin_txn_begin(&g_txn, ns);
		g_txn_active = true;
		printf("txn-begin ns=%u\n", ns);
		return 0;
	}

	if (!strcmp(cmd, "txn-stage") && argc >= 4) {
		if (!g_txn_active) die("txn-stage: no open txn");
		uint32_t h   = (uint32_t)strtoul(argv[1], NULL, 0);
		enum merlin_txn_op_type op = parse_op(argv[2]);
		uint64_t key = (uint64_t)strtoull(argv[3], NULL, 0);
		uint64_t val = argc >= 5
			? (uint64_t)strtoull(argv[4], NULL, 0) : 0;
		int rc = merlin_txn_stage(&g_txn, h, op, key, val);
		if (rc < 0)
			die("txn-stage: error %d (staged=%u max=%u)",
			    rc, g_txn.op_count, MERLIN_TXN_MAX_OPS);
		printf("txn-stage map=%u op=%s key=%lu val=%lu\n",
		       h, argv[2],
		       (unsigned long)key, (unsigned long)val);
		return 0;
	}

	if (!strcmp(cmd, "txn-commit")) {
		if (!g_txn_active) die("txn-commit: no open txn");
		uint32_t flags = MERLIN_TXN_F_DRAIN_RCU;
		if (argc >= 2 && !strcmp(argv[1], "fast"))
			flags = MERLIN_TXN_F_FAST;
		struct merlin_txn_stats_v1 st = {0};
		int rc = merlin_txn_commit(&g_txn, flags, &st);
		g_txn_active = false;
		if (rc == -EBUSY) {
			printf("txn-commit: CONFLICT (map=%u)\n",
			       st.conflict_map_handle);
			return 2;
		}
		if (rc < 0) die("txn-commit: error %d", rc);
		printf("txn-commit: OK staged=%u applied=%u skipped=%u"
		       " drain=%s time_ns=%lu\n",
		       st.ops_staged, st.ops_applied, st.ops_skipped,
		       (flags & MERLIN_TXN_F_DRAIN_RCU) ? "yes" : "no",
		       (unsigned long)st.commit_time_ns);
		return 0;
	}

	if (!strcmp(cmd, "txn-abort")) {
		if (g_txn_active) {
			merlin_txn_abort(&g_txn);
			g_txn_active = false;
		}
		printf("txn-abort\n");
		return 0;
	}

	fprintf(stderr, "unknown command '%s'\n", cmd);
	return 1;
}

static int run_script(const char *path)
{
	FILE *f = fopen(path, "r");
	if (!f) { perror("fopen"); return 1; }
	char line[512];
	int lineno = 0;
	while (fgets(line, sizeof(line), f)) {
		lineno++;
		/* strip newline and comments */
		char *p = line;
		while (*p && *p != '\n' && *p != '#') p++;
		*p = '\0';
		/* tokenize */
		char *toks[32];
		int ntok = 0;
		p = line;
		while (*p) {
			while (*p == ' ' || *p == '\t') p++;
			if (!*p) break;
			toks[ntok++] = p;
			while (*p && *p != ' ' && *p != '\t') p++;
			if (*p) *p++ = '\0';
		}
		if (!ntok) continue;
		int rc = exec_cmd(ntok, toks);
		if (rc != 0) {
			fprintf(stderr,
				"script %s:%d: command failed (rc=%d)\n",
				path, lineno, rc);
			fclose(f);
			return rc;
		}
	}
	fclose(f);
	return 0;
}

static void usage(void)
{
	fprintf(stderr,
		"Usage:\n"
		"  merlin-txn script <file.txn>\n"
		"  merlin-txn <cmd> [args...]   (map-create, txn-begin, ...)\n");
}

int main(int argc, char **argv)
{
	merlin_store_init();
	if (argc < 2) { usage(); return 2; }
	if (!strcmp(argv[1], "script") && argc == 3)
		return run_script(argv[2]);
	/* treat remaining argv as a single command */
	return exec_cmd(argc - 1, argv + 1);
}
