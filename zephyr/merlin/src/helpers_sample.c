/* SPDX-License-Identifier: Apache-2.0 */
/*
 * helpers_sample.c — built-in sample helpers (CONFIG_MERLIN_SAMPLE_HELPERS).
 *
 * Mirrors the smallest useful subset of the Linux dispatch.c helpers:
 *
 *   id  name                  signature
 *   ---  -------------------- -----------------------------
 *   1   trace_printk          (const char *fmt, uint32_t fmt_sz)
 *   2   ktime_uptime_ms       () -> u32
 *   3   rng_u32               () -> u32
 *   4   gpio_set              (uint32_t port, uint32_t pin, uint32_t v)
 *   5   gpio_get              (uint32_t port, uint32_t pin) -> u32
 *   6   yield                 ()
 *
 * Each helper is GPL-free / Apache-2.0 — these are application-side
 * stubs intended to be replaced or extended by integrators.
 */

#include "merlin_internal.h"

#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/random/random.h>
#include <zephyr/sys/printk.h>

static uint64_t h_trace_printk(uint64_t a0, uint64_t a1,
			       uint64_t a2, uint64_t a3,
			       uint64_t a4, uint64_t a5)
{
	const char *fmt = (const char *)(uintptr_t)a0;
	(void)a1; (void)a2; (void)a3; (void)a4; (void)a5;
	if (fmt)
		printk("merlin: %s\n", fmt);
	return 0;
}

static uint64_t h_uptime_ms(uint64_t a0, uint64_t a1,
			    uint64_t a2, uint64_t a3,
			    uint64_t a4, uint64_t a5)
{
	(void)a0; (void)a1; (void)a2; (void)a3; (void)a4; (void)a5;
	return (uint64_t)k_uptime_get_32();
}

static uint64_t h_rng_u32(uint64_t a0, uint64_t a1,
			  uint64_t a2, uint64_t a3,
			  uint64_t a4, uint64_t a5)
{
	(void)a0; (void)a1; (void)a2; (void)a3; (void)a4; (void)a5;
	return (uint64_t)sys_rand32_get();
}

static uint64_t h_gpio_set(uint64_t a0, uint64_t a1,
			   uint64_t a2, uint64_t a3,
			   uint64_t a4, uint64_t a5)
{
	/* Stub: real impl would dispatch through a registered devicetree
	 * gpio node by (port,pin).  For the prototype we just trace.
	 */
	(void)a3; (void)a4; (void)a5;
	printk("merlin: gpio_set(port=%u, pin=%u, v=%u)\n",
	       (unsigned)a0, (unsigned)a1, (unsigned)a2);
	return 0;
}

static uint64_t h_gpio_get(uint64_t a0, uint64_t a1,
			   uint64_t a2, uint64_t a3,
			   uint64_t a4, uint64_t a5)
{
	(void)a2; (void)a3; (void)a4; (void)a5;
	printk("merlin: gpio_get(port=%u, pin=%u) -> 0\n",
	       (unsigned)a0, (unsigned)a1);
	return 0;
}

static uint64_t h_yield(uint64_t a0, uint64_t a1,
			uint64_t a2, uint64_t a3,
			uint64_t a4, uint64_t a5)
{
	(void)a0; (void)a1; (void)a2; (void)a3; (void)a4; (void)a5;
	k_yield();
	return 0;
}

/* Register the built-ins at SYS_INIT time.  Use POST_KERNEL/APPLICATION
 * to guarantee k_malloc + mutex APIs are available.
 */
static int merlin_helpers_init(void)
{
	merlin_helper_register(1, "trace_printk", h_trace_printk);
	merlin_helper_register(2, "uptime_ms",    h_uptime_ms);
	merlin_helper_register(3, "rng_u32",      h_rng_u32);
	merlin_helper_register(4, "gpio_set",     h_gpio_set);
	merlin_helper_register(5, "gpio_get",     h_gpio_get);
	merlin_helper_register(6, "yield",        h_yield);
	return 0;
}

SYS_INIT(merlin_helpers_init, APPLICATION, CONFIG_KERNEL_INIT_PRIORITY_DEFAULT);
