// SPDX-License-Identifier: Apache-2.0
/*
 * smoke.c — host-side smoke test for the Zephyr-runtime decoder/verifier.
 *
 * Parses the same sample_blob.c the Zephyr sample uses, locates the
 * .text.merlin.* section directly, decodes each instruction, runs the
 * verifier, and asserts no rejections.
 *
 * This validates that the verifier and decoder can sit on top of the
 * blob the sample app embeds, without bringing in Zephyr.
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

#include "merlin_internal.h"

extern const uint8_t  sample_blob[];
extern const uint32_t sample_blob_len;

/* Mini Elf32 layout - just enough to find the text section */
struct Eh { uint8_t id[16]; uint16_t t, m; uint32_t v, ent, ph, sh, fl;
	    uint16_t es, phs, phn, shs, shn, shx; } __attribute__((packed));
struct Sh { uint32_t name, type, flags, addr, off, size, link, info, al, es; }
	__attribute__((packed));

static int find_text(const uint8_t *blob, size_t blen,
		     const uint8_t **t, uint32_t *tlen)
{
	const struct Eh *eh = (const void *)blob;
	const struct Sh *shstr;
	const char *names;

	if (memcmp(eh->id, "\x7f""ELF", 4) || eh->id[4] != 1)
		return -1;
	shstr = (const void *)(blob + eh->sh + eh->shx * eh->shs);
	names = (const char *)(blob + shstr->off);

	for (unsigned i = 1; i < eh->shn; i++) {
		const struct Sh *s = (const void *)(blob + eh->sh + i * eh->shs);
		if (s->type != 1)
			continue;
		if (strncmp(names + s->name, ".text.merlin.", 13) == 0) {
			*t    = blob + s->off;
			*tlen = s->size;
			printf("[smoke] found section '%s' size=%u\n",
			       names + s->name, s->size);
			return 0;
		}
	}
	return -1;
}

int main(void)
{
	const uint8_t *text = NULL;
	uint32_t text_len = 0;
	struct merlin_verifier_cfg vcfg;
	char log[512] = {0};
	int rc;

	printf("[smoke] sample_blob size = %u\n", sample_blob_len);

	rc = find_text(sample_blob, sample_blob_len, &text, &text_len);
	if (rc) {
		fprintf(stderr, "[smoke] FAIL: no text section in blob\n");
		return 1;
	}

	/* Decode every instruction and print it */
	for (uint32_t pc = 0; pc < text_len; pc += 4) {
		struct merlin_insn in;
		uint32_t w = (uint32_t)text[pc]
			   | ((uint32_t)text[pc+1] << 8)
			   | ((uint32_t)text[pc+2] << 16)
			   | ((uint32_t)text[pc+3] << 24);

		if (merlin_decode(w, pc, &in) < 0) {
			fprintf(stderr, "[smoke] FAIL: decode at pc=%u\n", pc);
			return 1;
		}
		printf("[smoke] pc=%u %08x  %s rd=x%u rs1=x%u rs2=x%u imm=%d\n",
		       pc, w, merlin_insn_class_name(in.cls),
		       in.rd, in.rs1, in.rs2, in.imm);
	}

	/* Verify */
	merlin_verifier_cfg_for_prog(&vcfg, MERLIN_RTOS_PROG_TYPE_UNSPEC);
	vcfg.log_buf    = log;
	vcfg.log_buf_sz = sizeof(log);

	rc = merlin_verify_text(text, text_len, &vcfg);
	printf("[smoke] verifier: %s\n", log);

	if (rc < 0) {
		fprintf(stderr, "[smoke] FAIL: verifier rejected\n");
		return 1;
	}

	printf("[smoke] PASS\n");
	return 0;
}
