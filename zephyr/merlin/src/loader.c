/* SPDX-License-Identifier: Apache-2.0 */
/*
 * loader.c — ELF parser + verify pipeline for the MERLIN-V Zephyr runtime.
 *
 * Zephyr has no libelf; we parse Elf32 headers directly using the byte
 * layout defined by the ELF specification.  Only enough of the format is
 * decoded to locate the first `.text.merlin.*` SHT_PROGBITS section,
 * validate bounds, and feed its bytes to the verifier.
 *
 * The rtos-rv32 profile is RV32, so we use Elf32 headers regardless of
 * the host Zephyr build.
 */

#include "merlin_internal.h"

#include <string.h>
#include <stdlib.h>
#include <zephyr/sys/printk.h>

/* Minimal Elf32 layout — keep self-contained so we don't depend on
 * a Zephyr-provided ELF header (some boards have it, some don't).
 */
#define EI_MAG0   0
#define ELFMAG    "\x7f""ELF"
#define ELFMAG_SZ 4
#define EI_CLASS  4
#define ELFCLASS32 1
#define EI_DATA   5
#define ELFDATA2LSB 1

#define ET_REL    1
#define EM_RISCV  243

#define SHT_PROGBITS 1
#define SHT_STRTAB   3

struct Elf32_Ehdr {
	uint8_t  e_ident[16];
	uint16_t e_type;
	uint16_t e_machine;
	uint32_t e_version;
	uint32_t e_entry;
	uint32_t e_phoff;
	uint32_t e_shoff;
	uint32_t e_flags;
	uint16_t e_ehsize;
	uint16_t e_phentsize;
	uint16_t e_phnum;
	uint16_t e_shentsize;
	uint16_t e_shnum;
	uint16_t e_shstrndx;
} __attribute__((packed));

struct Elf32_Shdr {
	uint32_t sh_name;
	uint32_t sh_type;
	uint32_t sh_flags;
	uint32_t sh_addr;
	uint32_t sh_offset;
	uint32_t sh_size;
	uint32_t sh_link;
	uint32_t sh_info;
	uint32_t sh_addralign;
	uint32_t sh_entsize;
} __attribute__((packed));

#define MERLIN_TEXT_PREFIX  ".text.merlin."
#define MERLIN_TEXT_PFX_LEN (sizeof(MERLIN_TEXT_PREFIX) - 1)

/* -----------------------------------------------------------------------
 * Forward decls from runtime.c (slot table) — defined there.
 * ----------------------------------------------------------------------- */
struct merlin_prog *merlin_alloc_slot(void);
void merlin_free_slot(struct merlin_prog *p);

/* -----------------------------------------------------------------------
 * Helpers
 * ----------------------------------------------------------------------- */

static int find_first_text_section(const uint8_t *blob, size_t len,
				   const uint8_t **text_out, uint32_t *tlen_out,
				   const char *name_out, size_t name_cap)
{
	const struct Elf32_Ehdr *eh;
	const struct Elf32_Shdr *shstr;
	const char *names;

	if (len < sizeof(*eh))
		return MERLIN_ERR_INVAL;
	eh = (const struct Elf32_Ehdr *)blob;

	if (memcmp(eh->e_ident, ELFMAG, ELFMAG_SZ) != 0)
		return MERLIN_ERR_INVAL;
	if (eh->e_ident[EI_CLASS] != ELFCLASS32)
		return MERLIN_ERR_INVAL;
	if (eh->e_ident[EI_DATA] != ELFDATA2LSB)
		return MERLIN_ERR_INVAL;
	if (eh->e_type != ET_REL)
		return MERLIN_ERR_INVAL;
	if (eh->e_machine != EM_RISCV)
		return MERLIN_ERR_INVAL;
	if (eh->e_shentsize < sizeof(struct Elf32_Shdr))
		return MERLIN_ERR_INVAL;
	if ((uint64_t)eh->e_shoff +
	    (uint64_t)eh->e_shnum * eh->e_shentsize > len)
		return MERLIN_ERR_INVAL;
	if (eh->e_shstrndx == 0 || eh->e_shstrndx >= eh->e_shnum)
		return MERLIN_ERR_INVAL;

	shstr = (const struct Elf32_Shdr *)(blob + eh->e_shoff +
		(uint32_t)eh->e_shstrndx * eh->e_shentsize);
	if (shstr->sh_type != SHT_STRTAB ||
	    (uint64_t)shstr->sh_offset + shstr->sh_size > len)
		return MERLIN_ERR_INVAL;
	names = (const char *)(blob + shstr->sh_offset);

	for (unsigned i = 1; i < eh->e_shnum; i++) {
		const struct Elf32_Shdr *sh = (const struct Elf32_Shdr *)
			(blob + eh->e_shoff + (uint32_t)i * eh->e_shentsize);
		const char *sname;

		if (sh->sh_type != SHT_PROGBITS)
			continue;
		if (sh->sh_name >= shstr->sh_size)
			continue;
		sname = names + sh->sh_name;

		if (strncmp(sname, MERLIN_TEXT_PREFIX, MERLIN_TEXT_PFX_LEN) != 0)
			continue;

		if ((uint64_t)sh->sh_offset + sh->sh_size > len)
			return MERLIN_ERR_INVAL;

		*text_out = blob + sh->sh_offset;
		*tlen_out = sh->sh_size;
		if (name_out && name_cap) {
			strncpy((char *)name_out, sname, name_cap - 1);
			((char *)name_out)[name_cap - 1] = '\0';
		}
		return MERLIN_OK;
	}

	return MERLIN_ERR_NOTFOUND;
}

/* -----------------------------------------------------------------------
 * merlin_prog_load — public API
 * ----------------------------------------------------------------------- */
int merlin_prog_load(const uint8_t *blob, size_t blob_len,
		     const struct merlin_load_attr *attr,
		     struct merlin_load_info *info,
		     struct merlin_prog **prog_out)
{
	const uint8_t *text = NULL;
	uint32_t text_len = 0;
	char sec_name[64] = {0};
	struct merlin_verifier_cfg vcfg;
	struct merlin_prog *prog = NULL;
	int rc;

	if (!blob || !blob_len || !prog_out || !info)
		return MERLIN_ERR_INVAL;
	if (blob_len > 64 * 1024)
		return MERLIN_ERR_E2BIG;

	rc = find_first_text_section(blob, blob_len, &text, &text_len,
				     sec_name, sizeof(sec_name));
	if (rc)
		return rc;

	if (text_len == 0 || text_len % 4)
		return MERLIN_ERR_INVAL;
	if (text_len > CONFIG_MERLIN_MAX_PROG_BYTES)
		return MERLIN_ERR_E2BIG;

	memset(info, 0, sizeof(*info));

	/* Verify */
	merlin_verifier_cfg_for_prog(&vcfg,
		attr ? attr->prog_type : MERLIN_RTOS_PROG_TYPE_UNSPEC);
	vcfg.verbose  = (CONFIG_MERLIN_LOG_LEVEL >= 2);
	vcfg.log_buf  = info->summary;
	vcfg.log_buf_sz = sizeof(info->summary);

	rc = merlin_verify_text(text, text_len, &vcfg);
	info->text_bytes     = text_len;
	info->verified_insns = vcfg.insns_seen;
	info->rejected       = vcfg.rejected;
	if (rc < 0)
		return MERLIN_ERR_VERIFY;

	/* Allocate slot and install */
	prog = merlin_alloc_slot();
	if (!prog)
		return MERLIN_ERR_NOMEM;

	if (attr && attr->name[0])
		strncpy(prog->name, attr->name, sizeof(prog->name) - 1);
	else
		strncpy(prog->name, "merlin-prog", sizeof(prog->name) - 1);

	prog->prog_type = attr ? attr->prog_type : MERLIN_RTOS_PROG_TYPE_UNSPEC;

	rc = merlin_runtime_install(prog, text, text_len);
	if (rc) {
		merlin_free_slot(prog);
		return rc;
	}

	info->prog_id = prog->id;
	*prog_out = prog;
	return MERLIN_OK;
}

int merlin_prog_unload(struct merlin_prog *prog)
{
	if (!prog || !prog->in_use)
		return MERLIN_ERR_INVAL;

	merlin_runtime_uninstall(prog);
	merlin_free_slot(prog);
	return MERLIN_OK;
}
