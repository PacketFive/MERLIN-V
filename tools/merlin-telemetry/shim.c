// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * shim.c - dispatch-shim implementation.
 */
#include "shim.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

static uint64_t now_ns(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

void merlin_prog_init(struct merlin_prog *prog,
		      merlin_prog_fn fn,
		      uint32_t prog_id,
		      const char *name,
		      uint64_t verifier_load_ns)
{
	memset(prog, 0, sizeof(*prog));
	prog->fn               = fn;
	prog->prog_id          = prog_id;
	prog->verifier_load_ns = verifier_load_ns;
	prog->stats.size       = (uint32_t)sizeof(struct merlin_prog_stats_v1);
	prog->stats.verifier_load_ns = verifier_load_ns;

	if (name) {
		size_t n = strlen(name);
		if (n >= sizeof(prog->prog_name))
			n = sizeof(prog->prog_name) - 1;
		memcpy(prog->prog_name, name, n);
		prog->prog_name[n] = '\0';
	}
}

uint64_t merlin_prog_invoke(struct merlin_prog *prog, void *ctx)
{
	uint64_t t0 = now_ns();
	uint64_t ret = prog->fn(ctx);
	uint64_t t1 = now_ns();

	uint64_t dt = (t1 >= t0) ? (t1 - t0) : 0;

	prog->stats.run_count++;
	prog->stats.run_ns_total += dt;
	prog->stats.last_run_time_ns = t1;

	/* Fold verdict into the 8-slot histogram.  For MVDP the legal
	 * range is 0..5 so the modulo is a no-op for valid returns;
	 * for any program type whose return value escapes the range,
	 * fold high bits into slot 0 (ABORTED). */
	unsigned idx = (unsigned)(ret) < MERLIN_STATS_V1_VERDICTS
			? (unsigned)(ret) : 0u;
	prog->stats.verdict_count[idx]++;

	return ret;
}

void merlin_prog_note_helper(struct merlin_prog *prog, int is_fault)
{
	prog->stats.helper_call_count++;
	if (is_fault)
		prog->stats.helper_fault_count++;
}

void merlin_prog_snapshot(const struct merlin_prog *prog,
			  struct merlin_prog_stats_v1 *out)
{
	*out = prog->stats;
}

int merlin_prog_render_text(const struct merlin_prog *prog,
			    char *out, size_t cap)
{
	const struct merlin_prog_stats_v1 *s = &prog->stats;

	int n = snprintf(out, cap,
		"prog_id:            %u\n"
		"prog_name:          %s\n"
		"size:               %u\n"
		"run_count:          %lu\n"
		"run_ns_total:       %lu\n"
		"verdict[0:ABORTED]: %lu\n"
		"verdict[1:DROP]:    %lu\n"
		"verdict[2:PASS]:    %lu\n"
		"verdict[3:TX]:      %lu\n"
		"verdict[4:REDIRECT]:%lu\n"
		"verdict[5:DELIVER]: %lu\n"
		"verdict[6]:         %lu\n"
		"verdict[7]:         %lu\n"
		"helper_call_count:  %lu\n"
		"helper_fault_count: %lu\n"
		"verifier_load_ns:   %lu\n"
		"last_run_time_ns:   %lu\n",
		prog->prog_id,
		prog->prog_name,
		s->size,
		(unsigned long)s->run_count,
		(unsigned long)s->run_ns_total,
		(unsigned long)s->verdict_count[0],
		(unsigned long)s->verdict_count[1],
		(unsigned long)s->verdict_count[2],
		(unsigned long)s->verdict_count[3],
		(unsigned long)s->verdict_count[4],
		(unsigned long)s->verdict_count[5],
		(unsigned long)s->verdict_count[6],
		(unsigned long)s->verdict_count[7],
		(unsigned long)s->helper_call_count,
		(unsigned long)s->helper_fault_count,
		(unsigned long)s->verifier_load_ns,
		(unsigned long)s->last_run_time_ns);

	if (n < 0 || (size_t)n >= cap)
		return -1;
	return n;
}
