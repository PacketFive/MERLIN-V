/* SPDX-License-Identifier: Apache-2.0 */
/*
 * helpers_esp32c3.c — board-tied MERLIN-V helpers for the DevKitM-1.
 *
 * Replaces the generic helpers from zephyr/merlin/src/helpers_sample.c
 * with implementations that drive the C3's real GPIO pins via the
 * Zephyr devicetree gpio nodes.  Useful for actuator-style programs
 * (e.g. "blink LED on packet match").
 *
 * Register at SYS_INIT before any MERLIN-V program loads.  Because
 * helper IDs match the generic helpers, programs that ran against
 * `helpers_sample.c` run unchanged against this implementation.
 */

#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/sys/printk.h>

#include "merlin/merlin.h"

/* The DevKitM-1 has a single user LED on GPIO8 (boot strapping
 * pin; some boards route it to a small SMD LED).  We resolve it via
 * the standard "led0" devicetree alias.
 */
#define LED0_NODE DT_ALIAS(led0)

#if DT_NODE_HAS_STATUS(LED0_NODE, okay)
static const struct gpio_dt_spec led0 = GPIO_DT_SPEC_GET(LED0_NODE, gpios);
#else
static const struct gpio_dt_spec led0 = {0};
#endif

static uint64_t h_led_set(uint64_t a0, uint64_t a1, uint64_t a2,
			  uint64_t a3, uint64_t a4, uint64_t a5)
{
	(void)a1; (void)a2; (void)a3; (void)a4; (void)a5;
	if (!device_is_ready(led0.port))
		return (uint64_t)-1;
	gpio_pin_set_dt(&led0, (int)a0);
	return 0;
}

static uint64_t h_led_toggle(uint64_t a0, uint64_t a1, uint64_t a2,
			     uint64_t a3, uint64_t a4, uint64_t a5)
{
	(void)a0; (void)a1; (void)a2; (void)a3; (void)a4; (void)a5;
	if (!device_is_ready(led0.port))
		return (uint64_t)-1;
	gpio_pin_toggle_dt(&led0);
	return 0;
}

static int merlin_esp32c3_helpers_init(void)
{
	if (device_is_ready(led0.port)) {
		gpio_pin_configure_dt(&led0, GPIO_OUTPUT_INACTIVE);
		printk("merlin/esp32c3: LED0 ready on %s pin %d\n",
		       led0.port->name, led0.pin);
	} else {
		printk("merlin/esp32c3: LED0 not available; helpers stubbed\n");
	}

	/* Helper IDs 4 (gpio_set) and 5 (gpio_get) are taken by the
	 * generic sample helpers; we register board-specific helpers at
	 * id 16+ to coexist.
	 */
	merlin_helper_register(16, "led_set",    h_led_set);
	merlin_helper_register(17, "led_toggle", h_led_toggle);
	return 0;
}

SYS_INIT(merlin_esp32c3_helpers_init, APPLICATION,
	 CONFIG_KERNEL_INIT_PRIORITY_DEFAULT);
