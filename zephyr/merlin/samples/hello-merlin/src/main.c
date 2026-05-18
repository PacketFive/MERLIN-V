/* SPDX-License-Identifier: Apache-2.0 */
/*
 * samples/hello-merlin/src/main.c — loads sample_blob, runs it, prints result.
 *
 * On a RISC-V Zephyr board (e.g. esp32c3_devkitm) this exercises the
 * full path: ELF parse → verify → install → run → unload.
 */

#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

#include "merlin/merlin.h"

extern const uint8_t  sample_blob[];
extern const uint32_t sample_blob_len;

int main(void)
{
	struct merlin_load_info info;
	merlin_prog_t *prog = NULL;
	uint64_t retval = 0;
	int rc;

	printk("merlin-hello: boot\n");

	rc = merlin_init();
	if (rc) {
		printk("merlin_init failed: %d\n", rc);
		return 0;
	}

	rc = merlin_prog_load(sample_blob, sample_blob_len, NULL, &info, &prog);
	if (rc) {
		printk("merlin_prog_load failed: %d (%s)\n", rc, info.summary);
		return 0;
	}

	printk("merlin-hello: loaded id=%u insns=%u\n",
	       info.prog_id, info.verified_insns);

#ifdef CONFIG_RISCV
	rc = merlin_prog_run(prog, NULL, &retval);
	if (rc)
		printk("merlin_prog_run failed: %d\n", rc);
	else
		printk("merlin-hello: prog returned %llu\n",
		       (unsigned long long)retval);
#else
	printk("merlin-hello: skip run (non-RISC-V host)\n");
	(void)retval;
#endif

	merlin_prog_unload(prog);
	printk("merlin: hello-merlin done\n");
	return 0;
}
