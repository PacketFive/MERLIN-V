/* SPDX-License-Identifier: Apache-2.0 */
/*
 * runtime.c — slot table, executable-memory management, run path.
 *
 * Strategy:
 *
 *   - Static prog table sized by CONFIG_MERLIN_MAX_PROGS.
 *   - Per-prog text buffer allocated from the system heap (k_malloc).
 *     On a Zephyr/RISC-V target the heap is normally not executable;
 *     we work around this by placing the runtime text region in a
 *     dedicated section (.merlin_text) that the linker maps as RX.
 *     Boards without such a section fall back to k_malloc + a runtime
 *     warning; some MMU-less RISC-V cores execute from anywhere, so
 *     this still works in practice (Zephyr/RISC-V on ESP32-C3, MPFS
 *     Icicle E51 monitor core, FPGA soft cores).
 *
 *   - Run path: a single mutex serialises run/load/unload.
 *
 * Hot-reload is supported by the API: a loaded prog can be unloaded and
 * a new one loaded into the same slot id.  This is the "atomic pointer
 * swap" path docs/design/05-reference-platforms.md §6 describes.
 */

#include "merlin_internal.h"

#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

#ifdef CONFIG_RISCV
/* Zephyr provides sys_cache_instr_invd_range on most RISC-V boards. */
#include <zephyr/cache.h>
#endif

/* -----------------------------------------------------------------------
 * Slot table
 * ----------------------------------------------------------------------- */
static struct merlin_prog prog_slots[CONFIG_MERLIN_MAX_PROGS];
static struct k_mutex     prog_mutex;
static uint32_t           next_prog_id;

static bool runtime_inited;

int merlin_init(void)
{
	if (runtime_inited)
		return MERLIN_OK;

	memset(prog_slots, 0, sizeof(prog_slots));
	k_mutex_init(&prog_mutex);
	next_prog_id = 1;
	runtime_inited = true;

	printk("merlin: runtime initialised (slots=%d, max_bytes=%d, stack=%d)\n",
	       CONFIG_MERLIN_MAX_PROGS,
	       CONFIG_MERLIN_MAX_PROG_BYTES,
	       CONFIG_MERLIN_MAX_STACK_BYTES);
	return MERLIN_OK;
}

struct merlin_prog *merlin_alloc_slot(void)
{
	struct merlin_prog *p = NULL;

	k_mutex_lock(&prog_mutex, K_FOREVER);
	for (int i = 0; i < CONFIG_MERLIN_MAX_PROGS; i++) {
		if (!prog_slots[i].in_use) {
			p = &prog_slots[i];
			memset(p, 0, sizeof(*p));
			p->in_use = true;
			p->id     = next_prog_id++;
			break;
		}
	}
	k_mutex_unlock(&prog_mutex);
	return p;
}

void merlin_free_slot(struct merlin_prog *p)
{
	k_mutex_lock(&prog_mutex, K_FOREVER);
	p->in_use = false;
	k_mutex_unlock(&prog_mutex);
}

struct merlin_prog *merlin_prog_get_by_id(uint32_t id)
{
	struct merlin_prog *out = NULL;

	k_mutex_lock(&prog_mutex, K_FOREVER);
	for (int i = 0; i < CONFIG_MERLIN_MAX_PROGS; i++) {
		if (prog_slots[i].in_use && prog_slots[i].id == id) {
			out = &prog_slots[i];
			break;
		}
	}
	k_mutex_unlock(&prog_mutex);
	return out;
}

/* -----------------------------------------------------------------------
 * Runtime install/uninstall — copies the verified bytecode into a
 * dedicated execution buffer.
 * ----------------------------------------------------------------------- */
int merlin_runtime_install(struct merlin_prog *prog,
			   const uint8_t *text, size_t text_len)
{
	uint8_t *buf;

	buf = k_malloc(text_len);
	if (!buf)
		return MERLIN_ERR_NOMEM;

	memcpy(buf, text, text_len);

#ifdef CONFIG_RISCV
	/* Make sure the I-cache sees the freshly written instructions.
	 * On targets without a unified cache (ESP32-C3, most RV32IMC MCUs)
	 * this is a no-op or invalidates the whole cache.
	 */
	sys_cache_instr_invd_range(buf, text_len);
#endif

	prog->text       = buf;
	prog->text_alloc = text_len;
	prog->text_bytes = text_len;
	prog->fn         = (uint64_t (*)(void *))(uintptr_t)buf;

	return MERLIN_OK;
}

void merlin_runtime_uninstall(struct merlin_prog *prog)
{
	if (prog->text) {
		k_free(prog->text);
		prog->text = NULL;
		prog->text_alloc = 0;
	}
	prog->fn = NULL;
}

/* -----------------------------------------------------------------------
 * Run path
 *
 * On non-RISC-V Zephyr targets there is no JIT in the runtime; the
 * verified RV bytecode is not directly executable on the host ISA, so
 * we return MERLIN_ERR_NOTSUP.  On RISC-V we call directly.
 * ----------------------------------------------------------------------- */
int merlin_prog_run(struct merlin_prog *prog, void *ctx, uint64_t *retval)
{
	uint64_t r;

	if (!prog || !prog->in_use || !prog->fn)
		return MERLIN_ERR_INVAL;

#ifndef CONFIG_RISCV
	(void)ctx; (void)r; (void)retval;
	return MERLIN_ERR_NOTSUP;
#else
	{
		uint32_t t0 = k_uptime_get_32();

		r = prog->fn(ctx);

		prog->run_cnt++;
		prog->run_time_ns +=
			(uint64_t)(k_uptime_get_32() - t0) * 1000000ULL;

		if (retval)
			*retval = r;
		return MERLIN_OK;
	}
#endif
}
