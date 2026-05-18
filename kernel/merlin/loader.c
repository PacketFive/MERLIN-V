// SPDX-License-Identifier: GPL-2.0-only
/*
 * loader.c — MERLIN-V ELF parser and program loader.
 *
 * Parses an ELF blob supplied by user space via MERLIN_PROG_LOAD,
 * locates the .text.merlin.<type>.<name> section(s), runs the verifier,
 * selects a JIT, and produces a struct merlin_prog.
 *
 * The kernel has no libelf, so we parse ELF headers directly using
 * <linux/elf.h>.  The parsing strategy is minimal but correct for the
 * subset of ELF that a GCC/Clang RISC-V cross-compiler produces for a
 * MERLIN-V object.
 *
 * Cross-references:
 *   docs/design/02-isa-and-bytecode.md §5   ELF section naming & layout
 *   docs/design/03-kernel-interfaces.md §2  MERLIN_PROG_LOAD UAPI
 *   docs/design/06-verifier.md              Verifier strategy
 *   docs/design/07-jit-and-offload.md       JIT selection
 */

#include <linux/bpf.h>   /* for bpf_map_* delegation, CAP_BPF check helpers */
#include <linux/elf.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/uaccess.h>
#include <linux/vmalloc.h>

#include "include/merlin_internal.h"

/* Prefix of all MERLIN-V text sections (per 02-isa-and-bytecode.md §5) */
#define MERLIN_TEXT_PREFIX  ".text.merlin."
#define MERLIN_TEXT_PFX_LEN (sizeof(MERLIN_TEXT_PREFIX) - 1)

/* -----------------------------------------------------------------------
 * validate_elf_header — sanity-check the ELF header for a MERLIN-V object.
 *
 * Accepted: ET_REL, EM_RISCV, little-endian, EI_CLASS = 64 or 32.
 * ----------------------------------------------------------------------- */
static int validate_elf_header(const Elf64_Ehdr *ehdr, size_t blob_len,
				const char **err)
{
	if (memcmp(ehdr->e_ident, ELFMAG, SELFMAG)) {
		*err = "not an ELF file";
		return -EINVAL;
	}
	if (ehdr->e_ident[EI_CLASS] != ELFCLASS64 &&
	    ehdr->e_ident[EI_CLASS] != ELFCLASS32) {
		*err = "unsupported ELF class (expected 32 or 64)";
		return -EINVAL;
	}
	if (ehdr->e_ident[EI_DATA] != ELFDATA2LSB) {
		*err = "big-endian ELF not supported";
		return -EINVAL;
	}
	if (ehdr->e_type != ET_REL) {
		*err = "not a relocatable object (ET_REL required)";
		return -EINVAL;
	}
	if (ehdr->e_machine != EM_RISCV) {
		*err = "not a RISC-V ELF (EM_RISCV required)";
		return -EINVAL;
	}
	if (ehdr->e_shentsize < sizeof(Elf64_Shdr)) {
		*err = "section header entry too small";
		return -EINVAL;
	}
	/* Check that the section table fits within the blob */
	if (ehdr->e_shoff >= blob_len ||
	    (u64)ehdr->e_shnum * ehdr->e_shentsize >
	    blob_len - ehdr->e_shoff) {
		*err = "section header table out of bounds";
		return -EINVAL;
	}
	return 0;
}

/* -----------------------------------------------------------------------
 * find_section — locate a section header and verify its data is in-bounds.
 *
 * Returns a pointer into the blob, or NULL with *err set.
 * ----------------------------------------------------------------------- */
static const Elf64_Shdr *find_section_hdr(const u8 *blob, size_t blob_len,
					  const Elf64_Ehdr *ehdr, u16 idx)
{
	const Elf64_Shdr *sh;

	if (idx == 0 || idx >= ehdr->e_shnum)
		return NULL;
	sh = (const Elf64_Shdr *)(blob + ehdr->e_shoff +
				  (u64)idx * ehdr->e_shentsize);
	if ((u8 *)sh + sizeof(*sh) > blob + blob_len)
		return NULL;
	if (sh->sh_type == SHT_NOBITS)
		return sh;  /* no data to bounds-check */
	if (sh->sh_offset >= blob_len || sh->sh_size > blob_len - sh->sh_offset)
		return NULL;
	return sh;
}

/* -----------------------------------------------------------------------
 * parse_and_load — core parsing loop.
 *
 * Called after the blob has been copied to kernel memory.
 * ----------------------------------------------------------------------- */
static int parse_and_load(const u8 *blob, size_t blob_len,
			  const union merlin_attr *attr,
			  struct merlin_prog **prog_out)
{
	const Elf64_Ehdr *ehdr = (const Elf64_Ehdr *)blob;
	const Elf64_Shdr *shstr_hdr;
	const char *shstrtab;
	size_t shstrtab_len;
	int rc;
	const char *err = "unknown error";
	unsigned int i;

	/* First .text.merlin.* section found: text + length */
	const u8 *text_data = NULL;
	size_t    text_len  = 0;
	char      prog_sec_name[64] = {0};

	struct merlin_verifier_cfg vcfg;
	struct merlin_jit_image   *img = NULL;
	struct merlin_prog        *prog = NULL;
	const struct merlin_jit_ops *jit;

	/* --- Validate ELF header --- */
	if (blob_len < sizeof(*ehdr)) {
		err = "blob too small for ELF header";
		return -EINVAL;
	}
	rc = validate_elf_header(ehdr, blob_len, &err);
	if (rc) {
		pr_debug("merlin_loader: ELF validation failed: %s\n", err);
		return rc;
	}

	/* --- Locate section-name string table --- */
	if (ehdr->e_shstrndx == SHN_XINDEX ||
	    ehdr->e_shstrndx == 0) {
		pr_debug("merlin_loader: missing shstrtab\n");
		return -EINVAL;
	}
	shstr_hdr = find_section_hdr(blob, blob_len, ehdr,
				     ehdr->e_shstrndx);
	if (!shstr_hdr || shstr_hdr->sh_type != SHT_STRTAB) {
		pr_debug("merlin_loader: shstrtab not found or wrong type\n");
		return -EINVAL;
	}
	shstrtab     = (const char *)(blob + shstr_hdr->sh_offset);
	shstrtab_len = shstr_hdr->sh_size;

	/* --- Walk section headers, find first .text.merlin.* --- */
	for (i = 1; i < ehdr->e_shnum; i++) {
		const Elf64_Shdr *sh =
			find_section_hdr(blob, blob_len, ehdr, i);
		const char *sname;

		if (!sh || sh->sh_type != SHT_PROGBITS)
			continue;
		if (sh->sh_name >= shstrtab_len)
			continue;

		sname = shstrtab + sh->sh_name;

		if (strncmp(sname, MERLIN_TEXT_PREFIX, MERLIN_TEXT_PFX_LEN) != 0)
			continue;

		/* Found a text section; take the first one for now.
		 * Phase 2: iterate all sections and verify each independently.
		 */
		if (!text_data) {
			text_data = blob + sh->sh_offset;
			text_len  = sh->sh_size;
			strscpy(prog_sec_name, sname, sizeof(prog_sec_name));
			pr_debug("merlin_loader: found text section [%s] "
				 "len=%zu\n", sname, text_len);
		}
	}

	if (!text_data) {
		pr_debug("merlin_loader: no .text.merlin.* section found\n");
		return -EINVAL;
	}

	if (text_len > (size_t)MERLIN_MAX_INSNS * 4) {
		pr_debug("merlin_loader: text too large (%zu > %u)\n",
			 text_len, MERLIN_MAX_INSNS * 4);
		return -E2BIG;
	}

	/* --- Set up verifier config --- */
	merlin_verifier_cfg_for_prog(&vcfg,
				     (enum merlin_prog_type)attr->prog_load.prog_type,
				     attr->prog_load.prog_flags);
	vcfg.verbose = !!(attr->prog_load.log_level & MERLIN_LOG_LEVEL_VERBOSE);

	/* Wire up log buffer if user supplied one */
	if (attr->prog_load.log_buf && attr->prog_load.log_size) {
		u32 sz = min_t(u32, attr->prog_load.log_size, MERLIN_LOG_BUF_MAX);

		vcfg.log_buf = kvmalloc(sz, GFP_KERNEL);
		if (!vcfg.log_buf)
			return -ENOMEM;
		vcfg.log_buf_sz = sz;
	}

	/* --- Verify --- */
	if (merlin_verify_text(text_data, text_len, 0, &vcfg)
	    != MERLIN_VERIFY_OK) {
		pr_debug("merlin_loader: verifier rejected program\n");
		rc = -EACCES;
		goto out_free_log;
	}

	/* --- Select JIT and translate --- */
	jit = merlin_select_jit((enum merlin_profile)attr->prog_load.profile);
	if (!jit) {
		pr_debug("merlin_loader: no JIT available for profile %u\n",
			 attr->prog_load.profile);
		rc = -ENODEV;
		goto out_free_log;
	}

	/* Allocate the prog object */
	prog = kzalloc(sizeof(*prog), GFP_KERNEL);
	if (!prog) {
		rc = -ENOMEM;
		goto out_free_log;
	}

	/* Copy bytecode */
	prog->bytecode = kmemdup(text_data, text_len, GFP_KERNEL);
	if (!prog->bytecode) {
		rc = -ENOMEM;
		goto out_free_prog;
	}
	prog->bytecode_len = text_len;

	/* Fill metadata */
	prog->prog_type = (enum merlin_prog_type)attr->prog_load.prog_type;
	prog->expected_attach_type =
		(enum merlin_attach_type)attr->prog_load.expected_attach_type;
	prog->profile = (enum merlin_profile)attr->prog_load.profile;
	prog->prog_flags = attr->prog_load.prog_flags;
	prog->load_time_ns = ktime_get_boot_ns();
	prog->created_by_uid = from_kuid_munged(&init_user_ns,
						current_uid());
	strscpy(prog->name, attr->prog_load.prog_name, MERLIN_OBJ_NAME_LEN);
	refcount_set(&prog->refs, 1);
	atomic64_set(&prog->run_cnt, 0);
	atomic64_set(&prog->run_time_ns, 0);
	atomic64_set(&prog->recursion_misses, 0);
	INIT_WORK(&prog->work_free, merlin_prog_free_work);

	/* JIT translate */
	rc = jit->translate(text_data, text_len, prog, &img);
	if (rc) {
		pr_debug("merlin_loader: JIT translate failed: %d\n", rc);
		goto out_free_bytecode;
	}

	rcu_assign_pointer(prog->jit_image, img);
	prog->prog_fn = (merlin_prog_fn)img->image;

	/* Assign an IDR id */
	rc = merlin_prog_alloc_id(prog);
	if (rc)
		goto out_free_jit;

	/* Copy verifier log to user if requested */
	if (vcfg.log_buf && vcfg.log_pos &&
	    attr->prog_load.log_buf && attr->prog_load.log_size) {
		if (copy_to_user((void __user *)(uintptr_t)attr->prog_load.log_buf,
				 vcfg.log_buf,
				 min_t(u32, vcfg.log_pos + 1,
				       attr->prog_load.log_size)))
			/* Non-fatal: the program is loaded; log copy failing
			 * is unfortunate but we do not roll back.
			 */
			pr_debug("merlin_loader: failed to copy log to user\n");
	}

	kvfree(vcfg.log_buf);
	*prog_out = prog;
	return 0;

out_free_jit:
	jit->free_image(img);
out_free_bytecode:
	kfree(prog->bytecode);
out_free_prog:
	kfree(prog);
out_free_log:
	kvfree(vcfg.log_buf);
	return rc;
}

/* -----------------------------------------------------------------------
 * merlin_prog_load — top-level entry point called from syscall.c
 * ----------------------------------------------------------------------- */
int merlin_prog_load(const union merlin_attr __user *uattr, u32 attr_sz,
		     struct merlin_prog **prog_out)
{
	union merlin_attr attr;
	u8 *blob = NULL;
	size_t blob_len;
	int rc;

	/* Copy the attr from user space (zero-pad unknown tail fields) */
	memset(&attr, 0, sizeof(attr));
	if (copy_from_user(&attr, uattr,
			   min_t(u32, attr_sz, sizeof(attr))))
		return -EFAULT;

	/* Basic sanity on the ELF blob pointer */
	if (!attr.prog_load.elf_ptr || !attr.prog_load.elf_len)
		return -EINVAL;

	blob_len = attr.prog_load.elf_len;
	if (blob_len > MERLIN_MAX_ELF_SIZE)
		return -E2BIG;

	blob = vmalloc(blob_len);
	if (!blob)
		return -ENOMEM;

	if (copy_from_user(blob, (const void __user *)(uintptr_t)
			   attr.prog_load.elf_ptr, blob_len)) {
		rc = -EFAULT;
		goto out;
	}

	rc = parse_and_load(blob, blob_len, &attr, prog_out);
out:
	vfree(blob);
	return rc;
}
EXPORT_SYMBOL_GPL(merlin_prog_load);
