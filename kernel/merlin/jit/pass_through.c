// SPDX-License-Identifier: GPL-2.0-only
/*
 * jit/pass_through.c — MERLIN-V pass-through JIT for RISC-V hosts.
 *
 * On a RISC-V Linux host the verified MERLIN-V bytecode is native machine
 * code: the "JIT step" is simply:
 *
 *   1. Apply ELF relocations that the verifier preserved (TODO: phase 2).
 *   2. Copy the text to a vmalloc'd RX region.
 *   3. Flush the I-cache over that region.
 *   4. Return the function pointer.
 *
 * No instruction translation is performed.
 *
 * Cross-reference: docs/design/07-jit-and-offload.md §3 (RISC-V path).
 */

#include <linux/module.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>

#ifdef CONFIG_RISCV
#include <asm/cacheflush.h>
#endif

#include "../include/merlin_internal.h"

static int passthrough_translate(const u8 *text, size_t text_len,
				 struct merlin_prog *prog,
				 struct merlin_jit_image **img_out)
{
	struct merlin_jit_image *img;
	void *exec;

	if (!text_len)
		return -EINVAL;

	img = kzalloc(sizeof(*img), GFP_KERNEL);
	if (!img)
		return -ENOMEM;

	/* Allocate executable memory */
	exec = vmalloc_exec(text_len);
	if (!exec) {
		kfree(img);
		return -ENOMEM;
	}

	memcpy(exec, text, text_len);

#ifdef CONFIG_RISCV
	/* Flush I-cache so the CPU sees the newly written instructions. */
	flush_icache_range((unsigned long)exec,
			   (unsigned long)exec + text_len);
#endif

	img->image     = exec;
	img->image_len = text_len;
	img->code_len  = text_len;

	*img_out = img;
	pr_debug("merlin: pass-through JIT: %zu bytes → %p\n",
		 text_len, exec);
	return 0;
}

static void passthrough_free_image(struct merlin_jit_image *img)
{
	if (img) {
		vfree(img->image);
		kfree(img);
	}
}

const struct merlin_jit_ops merlin_jit_passthrough = {
	.name        = "pass-through (RISC-V)",
	.translate   = passthrough_translate,
	.free_image  = passthrough_free_image,
};
EXPORT_SYMBOL_GPL(merlin_jit_passthrough);
