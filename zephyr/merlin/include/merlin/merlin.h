/* SPDX-License-Identifier: Apache-2.0 */
/*
 * include/merlin/merlin.h — Public API for the MERLIN-V Zephyr runtime.
 *
 * Embedded users include this header and use the four-call lifecycle:
 *
 *   1. merlin_init()             one-time, at boot
 *   2. merlin_prog_load(blob)    parse + verify + install a .merlin.o
 *   3. merlin_prog_run(prog, ctx) execute against a user-supplied ctx ptr
 *   4. merlin_prog_unload(prog)  free the slot
 *
 * The runtime is pass-through: on RISC-V the verified bytecode executes
 * natively after I-cache flush.  No JIT translation happens on this path.
 *
 * Cross-references:
 *   docs/design/02-isa-and-bytecode.md  ELF and ISA profile (merlin-rtos-rv32)
 *   docs/design/05-reference-platforms.md §6  MERLIN-V on Zephyr
 *   docs/design/06-verifier.md §7         Verifier profile (rtos-rv32/zephyr)
 */
#ifndef MERLIN_MERLIN_H_
#define MERLIN_MERLIN_H_

#include <stddef.h>
#include <stdint.h>
#ifndef MERLIN_HOST_SMOKE
#include <zephyr/sys/util.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* Opaque program handle */
typedef struct merlin_prog merlin_prog_t;

/* Program-type enumeration (RTOS subset of enum merlin_prog_type) */
enum merlin_rtos_prog_type {
	MERLIN_RTOS_PROG_TYPE_UNSPEC = 0,
	MERLIN_RTOS_PROG_TYPE_FILTER = 1,   /* generic packet/event filter   */
	MERLIN_RTOS_PROG_TYPE_PROBE  = 2,   /* tracing-style probe           */
	MERLIN_RTOS_PROG_TYPE_ACTUATE = 3,  /* actuator decision program     */
};

/* Result codes */
enum merlin_status {
	MERLIN_OK              = 0,
	MERLIN_ERR_INVAL       = -1,    /* malformed ELF / args              */
	MERLIN_ERR_E2BIG       = -2,    /* exceeds CONFIG_MERLIN_MAX_*       */
	MERLIN_ERR_NOMEM       = -3,    /* out of memory or slots            */
	MERLIN_ERR_VERIFY      = -4,    /* verifier rejected the program     */
	MERLIN_ERR_NOTSUP      = -5,    /* not supported on this target ISA  */
	MERLIN_ERR_NOTFOUND    = -6,
	MERLIN_ERR_BUSY        = -7,
};

/*
 * Load-info, returned to caller on a successful prog_load().
 *
 * The brief verifier line is always present (fits in 96 bytes); a
 * verbose log can be requested separately via the log_buf field on
 * the load_attr.
 */
struct merlin_load_info {
	uint32_t prog_id;
	uint32_t text_bytes;
	uint32_t verified_insns;
	uint32_t rejected;
	char     summary[96];
};

/*
 * Optional load attributes.  Pass NULL to use defaults.
 */
struct merlin_load_attr {
	enum merlin_rtos_prog_type prog_type;
	uint32_t flags;                  /* reserved, must be 0              */
	char     name[16];               /* informational                    */
	/* Optional verbose log */
	char    *log_buf;
	uint32_t log_buf_sz;
};

/*
 * One-time runtime initialisation.  Returns MERLIN_OK on success.
 */
int merlin_init(void);

/*
 * Load and verify a MERLIN-V ELF object.
 *
 * blob, blob_len: in-memory ELF.  The caller may free this immediately
 *                  after the call returns; the runtime copies what it
 *                  needs into the prog slot.
 * attr:           optional, may be NULL.
 * info:           filled with prog metadata on success.
 * prog:           opaque handle written on success.
 *
 * Returns MERLIN_OK or a negative merlin_status.
 */
int merlin_prog_load(const uint8_t *blob, size_t blob_len,
		     const struct merlin_load_attr *attr,
		     struct merlin_load_info *info,
		     merlin_prog_t **prog);

/*
 * Execute a loaded program against the supplied context pointer.
 *
 * The runtime calls the program with `ctx` as its first argument (a0
 * in the RISC-V calling convention).  The return value of the program
 * is written to *retval.
 *
 * Returns MERLIN_OK or MERLIN_ERR_INVAL.
 */
int merlin_prog_run(merlin_prog_t *prog, void *ctx, uint64_t *retval);

/*
 * Unload a previously loaded program.  Frees the prog slot and any
 * resources it held.  After this call, `prog` is invalid.
 */
int merlin_prog_unload(merlin_prog_t *prog);

/*
 * Lookup a prog by id (the id stored in struct merlin_load_info).
 * Returns NULL if not found.  The handle remains valid until the next
 * merlin_prog_unload() call on it.
 */
merlin_prog_t *merlin_prog_get_by_id(uint32_t id);

/*
 * Helper registry — embedded users may register their own helpers.
 *
 * Helper ABI:
 *   uint64_t fn(uint64_t a0, uint64_t a1, uint64_t a2,
 *               uint64_t a3, uint64_t a4, uint64_t a5);
 *
 * id range: 1..4095 (id 0 is reserved).  The verifier's allowlist is
 * configured at compile time per prog_type (see runtime config).
 */
typedef uint64_t (*merlin_helper_fn)(uint64_t, uint64_t, uint64_t,
				     uint64_t, uint64_t, uint64_t);

int merlin_helper_register(uint32_t id, const char *name,
			   merlin_helper_fn fn);

#ifdef __cplusplus
}
#endif

#endif /* MERLIN_MERLIN_H_ */
