// SPDX-License-Identifier: GPL-2.0-only
/*
 * jit/select.c — JIT backend selection for MERLIN-V.
 *
 * Called from loader.c to pick the right JIT ops for the current host.
 * On RISC-V hosts the pass-through backend is always selected; on other
 * hosts we select by compile-time host ISA.
 *
 * Cross-reference: docs/design/07-jit-and-offload.md §2 (JIT selection).
 */

#include <linux/module.h>

#include "../include/merlin_internal.h"

const struct merlin_jit_ops *merlin_select_jit(enum merlin_profile profile)
{
#if defined(CONFIG_MERLIN_JIT_RISCV) && defined(CONFIG_RISCV)
	(void)profile;
	return &merlin_jit_passthrough;
#elif defined(CONFIG_MERLIN_JIT_X86_64) && defined(CONFIG_X86_64)
	(void)profile;
	return &merlin_jit_x86_64;
#else
	(void)profile;
	return NULL;  /* no JIT compiled in for this host */
#endif
}
EXPORT_SYMBOL_GPL(merlin_select_jit);
