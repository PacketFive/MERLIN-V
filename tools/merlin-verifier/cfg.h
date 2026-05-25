/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * cfg.h - basic-block CFG construction and reducibility analysis.
 *
 * The Phase-2 verifier consumes a decoded instruction stream and
 * partitions it into basic blocks: contiguous instruction sequences
 * with a single entry (the leader) and a single exit (a control
 * transfer or fall-through).  We then compute:
 *
 *   - The set of forward and back edges.
 *   - Dominators (Cooper-Harvey-Kennedy iterative).
 *   - Whether every back edge B -> H has H dominating B
 *     (the standard reducibility criterion).
 *
 * Irreducible CFGs are rejected by the verifier.  This matches the
 * profile-compliance rule (CFG must be reducible -- see
 * docs/design/06-verifier.md §2).
 */
#ifndef MERLIN_VERIFIER_CFG_H
#define MERLIN_VERIFIER_CFG_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "decode.h"

#define MERLIN_CFG_MAX_BLOCKS 4096
#define MERLIN_CFG_MAX_EDGES (MERLIN_CFG_MAX_BLOCKS * 2)

struct merlin_block {
	uint32_t start_pc; /* byte offset of leader within .text       */
	uint32_t end_pc; /* byte offset of last instruction          */
	uint32_t insn_count;
	int succ[2]; /* up to two successors (-1 = none)         */
	uint32_t succ_pc[2]; /* successor leader PCs                     */
	bool is_return;
	bool reachable;
	int idom; /* immediate dominator (block index)        */
};

struct merlin_cfg {
	struct merlin_block blocks[MERLIN_CFG_MAX_BLOCKS];
	uint32_t nblocks;

	/* Reverse postorder (RPO) for the worklist algorithm. */
	int rpo[MERLIN_CFG_MAX_BLOCKS];
	uint32_t rpo_count;

	/* Back-edge set:  (src, dst) pairs.  An edge is a back edge iff
 * the destination dominates the source.
 */
	struct {
		int src;
		int dst;
	} back_edges[MERLIN_CFG_MAX_EDGES];
	uint32_t back_edge_count;

	bool reducible;
	const char *reject_reason;
};

/*
 * merlin_cfg_build - construct the CFG from a decoded instruction
 * stream.  text is the raw .text bytes; text_size must be a 4-byte
 * multiple; text_offset is the byte offset of text[0] within the
 * containing section (used only for diagnostic PCs).
 *
 * Returns 0 on success; -1 on construction error (e.g. malformed
 * branch, too many blocks).  On success cfg->reducible tells you
 * whether the program may be admitted; on irreducible CFGs the
 * reject_reason field carries a human-readable diagnostic.
 */
int merlin_cfg_build(const uint8_t *text, size_t text_size,
		     uint32_t text_offset, struct merlin_cfg *cfg);

/* Find a block by its leader PC.  Returns -1 if not found. */
int merlin_cfg_block_at(const struct merlin_cfg *cfg, uint32_t pc);

#endif /* MERLIN_VERIFIER_CFG_H */
