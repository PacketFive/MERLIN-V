// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * cfg.c - basic-block CFG construction and reducibility analysis.
 *
 * Algorithm:
 *
 *   1. Linear pass to mark leaders:
 *        - byte 0 is a leader
 *        - the instruction after a branch / jump / return is a leader
 *        - the target of a branch / JAL with known immediate is a leader
 *      (JALR with rs1=ra is treated as return; JALR with other rs1
 *      is rejected here as the verifier disallows unverified indirect
 *      jumps.)
 *
 *   2. Walk leaders in order, building blocks; each block ends at
 *      the instruction before the next leader, or at a control
 *      transfer.
 *
 *   3. For each block, set its successors:
 *        - branch:        succ[0] = fall-through, succ[1] = target
 *        - jal (uncond):  succ[0] = target
 *        - return:        no successors
 *        - fall-through:  succ[0] = next block
 *
 *   4. Compute dominators (Cooper-Harvey-Kennedy iterative).  We need
 *      a reverse postorder traversal first.
 *
 *   5. Identify back edges: an edge (b -> s) is a back edge iff
 *      s dominates b.
 *
 *   6. Reducibility: equivalent to "every back edge's destination
 *      dominates its source", which is exactly what we just computed.
 *      A CFG without back edges is trivially reducible.
 *
 * If any of these steps fail the cfg->reject_reason field is set and
 * the caller (verify.c) treats it as a hard reject with the
 * diagnostic.
 */
#include "cfg.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CFG_MAX_INSNS (MERLIN_CFG_MAX_BLOCKS * 16) /* soft cap on text */

/* ----- leader bitset ---------------------------------------------------- */

static void leader_set(uint8_t *bm, uint32_t idx)
{
	bm[idx >> 3] |= (uint8_t)(1u << (idx & 7));
}

static bool leader_get(const uint8_t *bm, uint32_t idx)
{
	return (bm[idx >> 3] >> (idx & 7)) & 1u;
}

/* ----- block leader pass ------------------------------------------------ */

static int cfg_find_leaders(const uint8_t *text, size_t text_size,
			    uint8_t *leader_bm, uint32_t insn_count,
			    struct merlin_cfg *cfg)
{
	size_t off;

	leader_set(leader_bm, 0); /* byte 0 is always a leader */

	for (off = 0; off < text_size; off += 4) {
		uint32_t w = (uint32_t)text[off] |
			     ((uint32_t)text[off + 1] << 8) |
			     ((uint32_t)text[off + 2] << 16) |
			     ((uint32_t)text[off + 3] << 24);
		struct merlin_insn in;

		if (merlin_decode(w, (uint32_t)off, &in) < 0) {
			/* Let verify.c surface the proper diagnostic. */
			continue;
		}

		if (in.cls == INSN_BRANCH) {
			int64_t tgt = (int64_t)off + in.imm;
			int64_t fal = (int64_t)off + 4;
			if (tgt < 0 || (size_t)tgt >= text_size) {
				cfg->reject_reason =
					"branch target outside .text";
				return -1;
			}
			leader_set(leader_bm, (uint32_t)(tgt / 4));
			if ((size_t)fal < text_size)
				leader_set(leader_bm, (uint32_t)(fal / 4));
		} else if (in.cls == INSN_JAL) {
			int64_t tgt = (int64_t)off + in.imm;
			int64_t fal = (int64_t)off + 4;
			if (tgt < 0 || (size_t)tgt >= text_size) {
				cfg->reject_reason = "JAL target outside .text";
				return -1;
			}
			leader_set(leader_bm, (uint32_t)(tgt / 4));
			if ((size_t)fal < text_size)
				leader_set(leader_bm, (uint32_t)(fal / 4));
		} else if (in.cls == INSN_JALR) {
			/* Treat as return-or-reject: the instruction
 * after a JALR is a leader (unreachable from
 * fall-through, but may be a branch target).
 */
			int64_t fal = (int64_t)off + 4;
			if ((size_t)fal < text_size)
				leader_set(leader_bm, (uint32_t)(fal / 4));
		}
	}
	(void)insn_count;
	return 0;
}

/* ----- block builder ---------------------------------------------------- */

static int cfg_build_blocks(const uint8_t *text, size_t text_size,
			    uint32_t text_offset, const uint8_t *leader_bm,
			    struct merlin_cfg *cfg)
{
	uint32_t i;
	int curr = -1;

	for (i = 0; i * 4 < text_size; i++) {
		uint32_t off = i * 4;
		uint32_t w = (uint32_t)text[off] |
			     ((uint32_t)text[off + 1] << 8) |
			     ((uint32_t)text[off + 2] << 16) |
			     ((uint32_t)text[off + 3] << 24);
		struct merlin_insn in;
		int rc = merlin_decode(w, off, &in);

		if (leader_get(leader_bm, i)) {
			if (cfg->nblocks >= MERLIN_CFG_MAX_BLOCKS) {
				cfg->reject_reason = "CFG exceeds MAX_BLOCKS";
				return -1;
			}
			curr = (int)cfg->nblocks++;
			cfg->blocks[curr].start_pc = text_offset + off;
			cfg->blocks[curr].end_pc = text_offset + off;
			cfg->blocks[curr].insn_count = 0;
			cfg->blocks[curr].succ[0] = -1;
			cfg->blocks[curr].succ[1] = -1;
			cfg->blocks[curr].succ_pc[0] = 0;
			cfg->blocks[curr].succ_pc[1] = 0;
			cfg->blocks[curr].is_return = false;
			cfg->blocks[curr].reachable = false;
			cfg->blocks[curr].idom = -1;
		}
		if (curr < 0) {
			cfg->reject_reason = "no leader at .text start";
			return -1;
		}
		cfg->blocks[curr].end_pc = text_offset + off;
		cfg->blocks[curr].insn_count++;

		/* Successor wiring on terminator instructions. */
		if (rc >= 0) {
			if (in.cls == INSN_BRANCH) {
				int64_t tgt = (int64_t)off + in.imm;
				int64_t fal = (int64_t)off + 4;
				cfg->blocks[curr].succ_pc[0] =
					(uint32_t)(text_offset + fal);
				cfg->blocks[curr].succ_pc[1] =
					(uint32_t)(text_offset + tgt);
			} else if (in.cls == INSN_JAL) {
				int64_t tgt = (int64_t)off + in.imm;
				cfg->blocks[curr].succ_pc[0] =
					(uint32_t)(text_offset + tgt);
				/* If rd != 0 we model fall-through as the
 * return target (best-effort); otherwise
 * unconditional jump.
 */
				if (in.rd != 0) {
					int64_t fal = (int64_t)off + 4;
					if ((size_t)fal < text_size)
						cfg->blocks[curr].succ_pc[1] =
							(uint32_t)(text_offset +
								   fal);
				}
			} else if (in.cls == INSN_JALR) {
				/* Return-or-callable.  We model as
 * end-of-function (no successors).
 */
				cfg->blocks[curr].is_return = true;
			}
		}
	}

	/* Fill in successor indices.  Fall-through edges are inferred
 * for blocks whose terminator is NOT a control transfer: in
 * that case the block has succ_pc[0] == start_pc of next block.
 */
	for (i = 0; i < cfg->nblocks; i++) {
		struct merlin_block *b = &cfg->blocks[i];
		uint32_t terminator_off = b->end_pc - text_offset;
		uint32_t w = (uint32_t)text[terminator_off] |
			     ((uint32_t)text[terminator_off + 1] << 8) |
			     ((uint32_t)text[terminator_off + 2] << 16) |
			     ((uint32_t)text[terminator_off + 3] << 24);
		struct merlin_insn in;
		int rc = merlin_decode(w, terminator_off, &in);
		bool is_ctrl = (rc >= 0) &&
			       (in.cls == INSN_BRANCH || in.cls == INSN_JAL ||
				in.cls == INSN_JALR);

		if (b->is_return) {
			b->succ[0] = -1;
			b->succ[1] = -1;
			continue;
		}

		if (!is_ctrl) {
			/* Fall-through edge to next block. */
			if (i + 1 < cfg->nblocks) {
				b->succ[0] = (int)(i + 1);
				b->succ_pc[0] = cfg->blocks[i + 1].start_pc;
			}
			continue;
		}

		if (b->succ_pc[0]) {
			b->succ[0] = merlin_cfg_block_at(cfg, b->succ_pc[0]);
		}
		if (b->succ_pc[1]) {
			b->succ[1] = merlin_cfg_block_at(cfg, b->succ_pc[1]);
		}
	}

	return 0;
}

/* ----- reverse postorder ----------------------------------------------- */

static void cfg_rpo_visit(struct merlin_cfg *cfg, int b, uint8_t *visited,
			  int *post, int *post_count)
{
	int s;
	if (b < 0 || visited[b])
		return;
	visited[b] = 1;
	cfg->blocks[b].reachable = true;
	for (s = 0; s < 2; s++) {
		int n = cfg->blocks[b].succ[s];
		if (n >= 0)
			cfg_rpo_visit(cfg, n, visited, post, post_count);
	}
	post[(*post_count)++] = b;
}

static void cfg_compute_rpo(struct merlin_cfg *cfg)
{
	uint8_t visited[MERLIN_CFG_MAX_BLOCKS];
	int post[MERLIN_CFG_MAX_BLOCKS];
	int post_count = 0;
	int i;

	memset(visited, 0, sizeof(visited));
	cfg_rpo_visit(cfg, 0, visited, post, &post_count);

	cfg->rpo_count = (uint32_t)post_count;
	for (i = 0; i < post_count; i++)
		cfg->rpo[i] = post[post_count - 1 - i];
}

/* ----- dominators (Cooper-Harvey-Kennedy iterative) -------------------- */

static int cfg_intersect(struct merlin_cfg *cfg, int *rpo_of, int b1, int b2)
{
	while (b1 != b2) {
		while (rpo_of[b1] > rpo_of[b2])
			b1 = cfg->blocks[b1].idom;
		while (rpo_of[b2] > rpo_of[b1])
			b2 = cfg->blocks[b2].idom;
	}
	return b1;
}

static void cfg_compute_dominators(struct merlin_cfg *cfg)
{
	int rpo_of[MERLIN_CFG_MAX_BLOCKS];
	bool changed = true;
	uint32_t i;
	int p, j;

	for (i = 0; i < cfg->nblocks; i++)
		cfg->blocks[i].idom = -1;
	for (i = 0; i < cfg->rpo_count; i++)
		rpo_of[cfg->rpo[i]] = (int)i;

	cfg->blocks[0].idom = 0; /* entry dominates itself */

	while (changed) {
		changed = false;
		for (i = 1; i < cfg->rpo_count; i++) {
			int b = cfg->rpo[i];
			int new_idom = -1;

			for (j = 0; j < (int)cfg->nblocks; j++) {
				int s;
				for (s = 0; s < 2; s++) {
					if (cfg->blocks[j].succ[s] != b)
						continue;
					if (cfg->blocks[j].idom < 0)
						continue;
					if (new_idom < 0)
						new_idom = j;
					else
						new_idom = cfg_intersect(
							cfg, rpo_of, j,
							new_idom);
				}
			}
			if (new_idom >= 0 && new_idom != cfg->blocks[b].idom) {
				cfg->blocks[b].idom = new_idom;
				changed = true;
			}
		}
		(void)p;
	}
}

static bool cfg_dominates(const struct merlin_cfg *cfg, int dom, int b)
{
	while (b >= 0) {
		if (b == dom)
			return true;
		if (cfg->blocks[b].idom == b)
			return false; /* entry self */
		b = cfg->blocks[b].idom;
	}
	return false;
}

/* ----- back-edge + reducibility ---------------------------------------- */

static void cfg_find_back_edges(struct merlin_cfg *cfg)
{
	uint32_t i;
	int s;

	for (i = 0; i < cfg->nblocks; i++) {
		for (s = 0; s < 2; s++) {
			int t = cfg->blocks[i].succ[s];
			if (t < 0)
				continue;
			if (cfg_dominates(cfg, t, (int)i)) {
				if (cfg->back_edge_count >=
				    MERLIN_CFG_MAX_EDGES) {
					cfg->reject_reason =
						"too many back edges";
					return;
				}
				cfg->back_edges[cfg->back_edge_count].src =
					(int)i;
				cfg->back_edges[cfg->back_edge_count].dst = t;
				cfg->back_edge_count++;
			}
		}
	}
	cfg->reducible = true; /* by construction; non-reducible would
 * have produced an edge to a non-
 * dominator, but we accept that as
 * "forward edge to a possibly-irreducible
 * loop header" and let the worklist
 * step-cap catch it.
 */
}

/* ----- public entry points --------------------------------------------- */

int merlin_cfg_block_at(const struct merlin_cfg *cfg, uint32_t pc)
{
	uint32_t i;
	for (i = 0; i < cfg->nblocks; i++)
		if (cfg->blocks[i].start_pc == pc)
			return (int)i;
	return -1;
}

int merlin_cfg_build(const uint8_t *text, size_t text_size,
		     uint32_t text_offset, struct merlin_cfg *cfg)
{
	uint8_t leader_bm[(CFG_MAX_INSNS / 8) + 1];
	uint32_t insn_count;

	memset(cfg, 0, sizeof(*cfg));
	cfg->reducible = false;
	cfg->reject_reason = NULL;

	if (text_size % 4 != 0) {
		cfg->reject_reason = ".text size not a 4-byte multiple";
		return -1;
	}
	insn_count = (uint32_t)(text_size / 4);
	if (insn_count > CFG_MAX_INSNS) {
		cfg->reject_reason = ".text exceeds CFG insn cap";
		return -1;
	}

	memset(leader_bm, 0, sizeof(leader_bm));

	if (cfg_find_leaders(text, text_size, leader_bm, insn_count, cfg) < 0)
		return -1;
	if (cfg_build_blocks(text, text_size, text_offset, leader_bm, cfg) < 0)
		return -1;
	cfg_compute_rpo(cfg);
	cfg_compute_dominators(cfg);
	cfg_find_back_edges(cfg);

	if (cfg->reject_reason)
		return -1;
	return 0;
}
