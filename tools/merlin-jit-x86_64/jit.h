/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * jit.h - MERLIN-V (RV64) -> x86_64 host JIT.
 *
 * Inputs: a verified MERLIN-V .text byte stream (little-endian 32-bit
 * RISC-V instructions) plus a helper-trampoline function pointer.
 * Output: an executable mmap region containing the translated host
 * code, plus a function pointer typed
 *
 *     typedef uint64_t (*merlin_jit_fn)(void *ctx);
 *
 * The translator is a single linear pass.  No CFG analysis, no
 * register allocation.  Each RV instruction maps to a small fixed
 * x86_64 sequence that loads operands from a 32-slot uint64_t
 * regs[] array (held on the JIT'd function's stack frame),
 * computes, and stores back.
 *
 * Helper calls (ecall) are emitted as a call into a C trampoline.
 * The trampoline reads a7 / a0..a5 from regs[], dispatches to the
 * registered helper, and writes the result back to a0.
 */
#ifndef MERLIN_JIT_H
#define MERLIN_JIT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*
 * Helper trampoline signature:
 *
 *   void (*trampoline)(uint64_t *regs);
 *
 * The trampoline reads helper_id = regs[17] (a7), reads args from
 * regs[10..15] (a0..a5), looks up the helper, executes it, and
 * writes the return value to regs[10] (a0).
 *
 * The JIT calls the trampoline with &regs[0] in rdi (SysV arg 0).
 */
typedef void (*merlin_helper_trampoline)(uint64_t *regs);

typedef uint64_t (*merlin_jit_fn)(void *ctx);

struct merlin_jit_result {
	void                  *exec;       /* mmap'd RX page              */
	size_t                 exec_len;
	size_t                 code_bytes; /* useful x86 bytes emitted    */
	merlin_jit_fn          fn;         /* equal to exec; convenience  */
	bool                   ok;
	char                   error[160];
};

/*
 * Translate one .text.merlin.* section.  text/text_size is the raw
 * RV64 instruction stream as little-endian bytes.  trampoline is
 * invoked from inside the JIT'd function on every ecall.
 *
 * On success out->ok = true and out->fn is callable.  On failure
 * out->ok = false and out->error contains a human-readable string.
 *
 * The caller frees out->exec via merlin_jit_free().
 */
int merlin_jit_translate(const uint8_t *text, size_t text_size,
			 merlin_helper_trampoline tramp,
			 struct merlin_jit_result *out);

void merlin_jit_free(struct merlin_jit_result *out);

#endif /* MERLIN_JIT_H */
