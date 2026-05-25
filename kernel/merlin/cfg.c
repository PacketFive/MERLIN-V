// SPDX-License-Identifier: GPL-2.0-only
/*
 * cfg.c - basic-block CFG construction and reducibility analysis.
 *
 * Kernel port of tools/merlin-verifier/cfg.c.  The algorithm is
 * unchanged; only the integer types switch from C99 stdint to
 * the kernel's u/s aliases, and recursive helpers are converted to
 * iterative form (kernel stacks are small).
 *
 * Algorithm:
 *
 *   1. Linear pass to mark leaders: byte 0; insn after a control
 *      transfer; target of every direct branch/JAL.
 *   2. Walk leaders to build blocks.
 *   3. Wire successors based on the terminator of each block.
 *   4. Compute reverse postorder by DFS from block 0.
 *   5. Compute immediate dominators (Cooper-Harvey-Kennedy iterative).
 *   6. Identify back edges (destination dominates source).
 *
 * See docs/design/06-verifier.md §2 (CFG reducibility requirement).
 */

#include <linux/module.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/types.h>

#include "include/merlin_internal.h"

#define CFG_MAX_INSNS (MERLIN_CFG_MAX_BLOCKS * 16)

/* ----- leader bitset ----- */
static void leader_set(u8 *bm, u32 idx)
{
	bm[idx >> 3] |= (u8)(1u << (idx & 7));
}
static bool leader_get(const u8 *bm, u32 idx)
{
	return (bm[idx >> 3] >> (idx & 7)) & 1u;
}

static int cfg_find_leaders(const u8 *text, size_t text_size, u8 *leader_bm,
			    struct merlin_cfg *cfg)
{
	size_t off;
	leader_set(leader_bm, 0);

	for (off = 0; off < text_size; off += 4) {
		u32 w = (u32)text[off] | ((u32)text[off + 1] << 8) |
			((u32)text[off + 2] << 16) | ((u32)text[off + 3] << 24);
		struct merlin_insn in;

		if (merlin_decode(w, (u32)off, &in) < 0)
			continue;

		if (in.cls == INSN_BRANCH || in.cls == INSN_JAL) {
			s64 tgt = (s64)off + in.imm;
			s64 fal = (s64)off + 4;
			if (tgt < 0 || (size_t)tgt >= text_size) {
				cfg->reject_reason =
					"branch/JAL target outside .text";
				return -1;
			}
			leader_set(leader_bm, (u32)(tgt / 4));
			if ((size_t)fal < text_size)
				leader_set(leader_bm, (u32)(fal / 4));
		} else if (in.cls == INSN_JALR) {
			s64 fal = (s64)off + 4;
			if ((size_t)fal < text_size)
				leader_set(leader_bm, (u32)(fal / 4));
		}
	}
	return 0;
}

static int cfg_build_blocks(const u8 *text, size_t text_size, u32 text_offset,
			    const u8 *leader_bm, struct merlin_cfg *cfg)
{
	u32 i;
	int curr = -1;

	for (i = 0; i * 4 < text_size; i++) {
		u32 off = i * 4;
		u32 w = (u32)text[off] | ((u32)text[off + 1] << 8) |
			((u32)text[off + 2] << 16) | ((u32)text[off + 3] << 24);
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

		if (rc >= 0) {
			if (in.cls == INSN_BRANCH) {
				s64 tgt = (s64)off + in.imm;
				s64 fal = (s64)off + 4;
				cfg->blocks[curr].succ_pc[0] =
					(u32)(text_offset + fal);
				cfg->blocks[curr].succ_pc[1] =
					(u32)(text_offset + tgt);
			} else if (in.cls == INSN_JAL) {
				s64 tgt = (s64)off + in.imm;
				cfg->blocks[curr].succ_pc[0] =
					(u32)(text_offset + tgt);
				if (in.rd != 0) {
					s64 fal = (s64)off + 4;
					if ((size_t)fal < text_size)
						cfg->blocks[curr].succ_pc[1] =
							(u32)(text_offset +
							      fal);
				}
			} else if (in.cls == INSN_JALR) {
				cfg->blocks[curr].is_return = true;
			}
		}
	}

	for (i = 0; i < cfg->nblocks; i++) {
		struct merlin_block *b = &cfg->blocks[i];
		u32 toff = b->end_pc - text_offset;
		u32 w = (u32)text[toff] | ((u32)text[toff + 1] << 8) |
			((u32)text[toff + 2] << 16) |
			((u32)text[toff + 3] << 24);
		struct merlin_insn in;
		int rc = merlin_decode(w, toff, &in);
		bool is_ctrl = (rc >= 0) &&
			       (in.cls == INSN_BRANCH || in.cls == INSN_JAL ||
				in.cls == INSN_JALR);

		if (b->is_return) {
			b->succ[0] = -1;
			b->succ[1] = -1;
			continue;
		}
		if (!is_ctrl) {
			if (i + 1 < cfg->nblocks) {
				b->succ[0] = (int)(i + 1);
				b->succ_pc[0] = cfg->blocks[i + 1].start_pc;
			}
			continue;
		}
		if (b->succ_pc[0])
			b->succ[0] = merlin_cfg_block_at(cfg, b->succ_pc[0]);
		if (b->succ_pc[1])
			b->succ[1] = merlin_cfg_block_at(cfg, b->succ_pc[1]);
	}
	return 0;
}

/* ----- iterative DFS-postorder (kernel stacks are small) ----- */
static void cfg_compute_rpo(struct merlin_cfg *cfg)
{
	int *stack;
	u8 *state;
	int *post;
	int top = 0, post_count = 0, i;

	stack = kmalloc_array(MERLIN_CFG_MAX_BLOCKS, sizeof(*stack), GFP_KERNEL);
	state = kzalloc(MERLIN_CFG_MAX_BLOCKS, GFP_KERNEL);
	post = kmalloc_array(MERLIN_CFG_MAX_BLOCKS, sizeof(*post), GFP_KERNEL);
	if (!stack || !state || !post) {
		cfg->reject_reason = "ENOMEM in RPO";
		goto out;
	}

	stack[top++] = 0;
	while (top > 0) {
		int b = stack[top - 1];
		if (state[b] == 0) {
			state[b] = 1;
			cfg->blocks[b].reachable = true;
		}
		{
			int next = -1, s;
			for (s = 0; s < 2; s++) {
				int n = cfg->blocks[b].succ[s];
				if (n >= 0 && state[n] == 0) {
					next = n;
					break;
				}
			}
			if (next >= 0) {
				stack[top++] = next;
			} else {
				state[b] = 2;
				post[post_count++] = b;
				top--;
			}
		}
	}
	cfg->rpo_count = (u32)post_count;
	for (i = 0; i < post_count; i++)
		cfg->rpo[i] = post[post_count - 1 - i];
out:
	kfree(stack);
	kfree(state);
	kfree(post);
}

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
	static int rpo_of[MERLIN_CFG_MAX_BLOCKS]; /* large; static avoids stack */
	bool changed = true;
	u32 i;
	int j;

	for (i = 0; i < cfg->nblocks; i++)
		cfg->blocks[i].idom = -1;
	for (i = 0; i < cfg->rpo_count; i++)
		rpo_of[cfg->rpo[i]] = (int)i;
	cfg->blocks[0].idom = 0;

	while (changed) {
		changed = false;
		for (i = 1; i < cfg->rpo_count; i++) {
			int b = cfg->rpo[i];
			int new_idom = -1, s;

			for (j = 0; j < (int)cfg->nblocks; j++) {
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
	}
}

static bool cfg_dominates(const struct merlin_cfg *cfg, int dom, int b)
{
	while (b >= 0) {
		if (b == dom)
			return true;
		if (cfg->blocks[b].idom == b)
			return false;
		b = cfg->blocks[b].idom;
	}
	return false;
}

static void cfg_find_back_edges(struct merlin_cfg *cfg)
{
	u32 i;
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
	cfg->reducible = true;
}

/* ----- public ----- */
int merlin_cfg_block_at(const struct merlin_cfg *cfg, u32 pc)
{
	u32 i;
	for (i = 0; i < cfg->nblocks; i++)
		if (cfg->blocks[i].start_pc == pc)
			return (int)i;
	return -1;
}

int merlin_cfg_build(const u8 *text, size_t text_size, u32 text_offset,
		     struct merlin_cfg *cfg)
{
	u8 *leader_bm;
	u32 insn_count;

	memset(cfg, 0, sizeof(*cfg));
	cfg->reducible = false;
	cfg->reject_reason = NULL;

	if (text_size % 4 != 0) {
		cfg->reject_reason = ".text size not a 4-byte multiple";
		return -1;
	}
	insn_count = (u32)(text_size / 4);
	if (insn_count > CFG_MAX_INSNS) {
		cfg->reject_reason = ".text exceeds CFG insn cap";
		return -1;
	}

	leader_bm = kzalloc((CFG_MAX_INSNS / 8) + 1, GFP_KERNEL);
	if (!leader_bm) {
		cfg->reject_reason = "ENOMEM";
		return -1;
	}

	if (cfg_find_leaders(text, text_size, leader_bm, cfg) < 0)
		goto out_err;
	if (cfg_build_blocks(text, text_size, text_offset, leader_bm, cfg) < 0)
		goto out_err;
	cfg_compute_rpo(cfg);
	cfg_compute_dominators(cfg);
	cfg_find_back_edges(cfg);

	kfree(leader_bm);
	if (cfg->reject_reason)
		return -1;
	return 0;
out_err:
	kfree(leader_bm);
	return -1;
}
