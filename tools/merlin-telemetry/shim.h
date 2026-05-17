/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * shim.h - the dispatch shim that wraps a JIT'd MERLIN-V program
 *          and emits the canonical per-program counter block.
 *
 * In the eventual kernel implementation this lives in
 * kernel/merlin/dispatch.c and runs before every invocation of a
 * loaded program.  In user space it wraps a merlin_jit_fn the
 * caller obtained from merlin_jit_translate().
 *
 * The counter block emitted is byte-for-byte the layout pinned in
 * docs/design/09-mvcp-kernel-uapi.md §3.5 and exposed as
 * struct merlin_prog_stats_v1 in <merlin/stats.h>.
 */
#ifndef MERLIN_TELEMETRY_SHIM_H
#define MERLIN_TELEMETRY_SHIM_H

#include <stdint.h>
#include <stddef.h>

#include "merlin/stats.h"

/* Use the same prototype the JIT exposes; we don't include jit.h
 * here so the telemetry shim can be linked against either a real
 * JIT'd function or a unit-test stub.
 */
typedef uint64_t (*merlin_prog_fn)(void *ctx);

struct merlin_prog {
	merlin_prog_fn               fn;
	struct merlin_prog_stats_v1  stats;
	uint64_t                     verifier_load_ns;
	uint32_t                     prog_id;
	char                         prog_name[32];
};

/*
 * Initialise a prog wrapper.  prog->fn must be set before
 * invoke(); the shim does not own the JIT'd page.
 *
 * verifier_load_ns is recorded once in stats.verifier_load_ns
 * and never updated.
 */
void merlin_prog_init(struct merlin_prog *prog,
		      merlin_prog_fn fn,
		      uint32_t prog_id,
		      const char *name,
		      uint64_t verifier_load_ns);

/*
 * Run prog->fn(ctx) and update prog->stats.  Returns whatever
 * the program returned.
 *
 * The shim measures CLOCK_MONOTONIC ns around the call, increments
 * run_count, adds the elapsed ns to run_ns_total, and increments
 * verdict_count[ret & 7] (high bits folded; the verifier has
 * already constrained the return value to a small range).
 */
uint64_t merlin_prog_invoke(struct merlin_prog *prog, void *ctx);

/*
 * Manually record a helper call / fault from the trampoline.
 * Useful for tests that bypass the real trampoline.
 */
void merlin_prog_note_helper(struct merlin_prog *prog, int is_fault);

/*
 * Atomically snapshot stats; the snapshot is the on-disk wire
 * format, suitable for writing to tracefs binary or netlink.
 */
void merlin_prog_snapshot(const struct merlin_prog *prog,
			  struct merlin_prog_stats_v1 *out);

/*
 * Render stats as the tracefs-style human-readable text format
 * (per design 09 §3.5).  Returns bytes written (excluding the
 * trailing NUL); -1 on truncation.
 */
int merlin_prog_render_text(const struct merlin_prog *prog,
			    char *out, size_t cap);

#endif /* MERLIN_TELEMETRY_SHIM_H */
