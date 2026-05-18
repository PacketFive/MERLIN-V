/* SPDX-License-Identifier: Apache-2.0 */
/*
 * sample-classifier/src/main.c — ESP32-C3 packet-classifier demo.
 *
 * Loads classifier_blob.c at runtime, runs it against synthetic Ethernet
 * frames (one IPv4, one ARP), prints the verdict for each.  Demonstrates
 * the end-to-end load → verify → run path on a 400 KiB SRAM RV32IMC MCU
 * with no MMU.
 *
 * The classifier is the tiniest non-trivial filter that exercises the
 * verifier's pointer-provenance and forward-branch paths.  See
 * classifier_blob.c for the bytecode source.
 */

#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

#include "merlin/merlin.h"

extern const uint8_t  classifier_blob[];
extern const uint32_t classifier_blob_len;

/* Two synthetic Ethernet headers used as the ctx pointer for the
 * classifier.  Only the high byte of EtherType at offset 12 matters
 * to the classifier.
 *
 * IPv4 EtherType = 0x0800   →  byte[12] = 0x08  → verdict = 2 (PASS)
 * ARP  EtherType = 0x0806   →  byte[12] = 0x08  → verdict = 2 (PASS)
 *
 * For the "DROP" case we use an EtherType that isn't IPv4-family, e.g.
 * RARP (0x8035) → byte[12] = 0x80 → verdict = 1 (DROP).
 *
 * (The point isn't a meaningful classification — it's that the program
 * dispatches based on packet contents the verifier proved came from
 * a typed pointer.)
 */
static const uint8_t pkt_ipv4[16] = {
	/* dst MAC */     0xff,0xff,0xff,0xff,0xff,0xff,
	/* src MAC */     0x02,0x00,0x00,0x00,0x00,0x01,
	/* etype hi/lo */ 0x08, 0x00,
	/* payload */     0x00, 0x00,
};

static const uint8_t pkt_rarp[16] = {
	0xff,0xff,0xff,0xff,0xff,0xff,
	0x02,0x00,0x00,0x00,0x00,0x01,
	0x80, 0x35,
	0x00, 0x00,
};

static const char *verdict_str(uint64_t v)
{
	switch (v) {
	case 1: return "DROP";
	case 2: return "PASS";
	default: return "?";
	}
}

int main(void)
{
	struct merlin_load_info info;
	merlin_prog_t *prog = NULL;
	uint64_t retval;
	int rc;

	printk("classifier: boot on esp32c3 (RV32IMC, no MMU)\n");

	rc = merlin_init();
	if (rc) {
		printk("merlin_init failed: %d\n", rc);
		return 0;
	}

	rc = merlin_prog_load(classifier_blob, classifier_blob_len,
			      NULL, &info, &prog);
	if (rc) {
		printk("merlin_prog_load failed rc=%d log='%s'\n",
		       rc, info.summary);
		return 0;
	}
	printk("classifier: loaded id=%u insns=%u\n",
	       info.prog_id, info.verified_insns);

#ifdef CONFIG_RISCV
	/* IPv4 packet: byte 12 == 0x08 → PASS */
	rc = merlin_prog_run(prog, (void *)pkt_ipv4, &retval);
	if (rc)
		printk("run(ipv4) failed: %d\n", rc);
	else
		printk("classifier: synthetic packet ETH/IPv4/TCP "
		       "\u2192 verdict=%llu (%s)\n",
		       (unsigned long long)retval, verdict_str(retval));

	/* RARP packet: byte 12 == 0x80 → DROP */
	rc = merlin_prog_run(prog, (void *)pkt_rarp, &retval);
	if (rc)
		printk("run(rarp) failed: %d\n", rc);
	else
		printk("classifier: synthetic packet ETH/ARP    "
		       "\u2192 verdict=%llu (%s)\n",
		       (unsigned long long)retval, verdict_str(retval));
#else
	(void)retval;
	printk("classifier: skip run (non-RISC-V host)\n");
#endif

	merlin_prog_unload(prog);
	printk("classifier: done\n");
	return 0;
}
