/* SPDX-License-Identifier: GPL-2.0 OR BSD-2-Clause */
/*
 * <merlin/section_macros.h> - ELF section placement macros for
 * MERLIN-V programs.
 *
 * A MERLIN-V program is an ELF object whose section names follow
 * a fixed convention (docs/design/02-isa-and-bytecode.md §8.1):
 *
 *    .text.<prog_name>       a program function
 *    .merlin.meta            mandatory; merlin_meta_v1
 *    .merlin.license         SPDX string
 *    .merlin.maps            map descriptors
 *    .merlin.relocs          relocation entries
 *    .merlin.btf             MERLIN BTF type information
 *    .merlin.sig             optional signature (MVCP signed-progs)
 *
 * Programs use SEC(name) to place a symbol in a specific section.
 * Section-name strings are user-visible UAPI: the loader matches
 * them by exact bytes.
 */
#ifndef _MERLIN_SECTION_MACROS_H
#define _MERLIN_SECTION_MACROS_H

/* SEC("...") - place the following declaration in a named ELF section.
 *
 * Implementation: __attribute__((section("..."))) plus a "used"
 * attribute so dead-code elimination does not drop the symbol.
 */
#define SEC(NAME) __attribute__((section(NAME), used))

/* Program-section helpers.  These produce the canonical section name
 * the verifier expects for each program type.
 *
 *    MERLIN_PROG_MVDP(name)          .text.merlin.mvdp.<name>
 *    MERLIN_PROG_XDP_V(name)         .text.merlin.xdp_v.<name>
 *    MERLIN_PROG_TC_V(name)          .text.merlin.tc_v.<name>
 *    MERLIN_PROG_KPROBE_V(name)      .text.merlin.kprobe_v.<name>
 *    MERLIN_PROG_TRACEPOINT_V(name)  .text.merlin.tracepoint_v.<name>
 *    MERLIN_PROG_SOCKET_FILTER_V(n)  .text.merlin.sockfilt_v.<name>
 *    MERLIN_PROG_PERF_EVENT_V(name)  .text.merlin.perfev_v.<name>
 *
 * The loader uses the prefix to derive the program type if the
 * caller did not set it explicitly via union merlin_attr.prog_load.
 */
#define _MERLIN_STR(s) #s
#define _MERLIN_CAT(a, b) a##b
#define _MERLIN_SECNAME(prefix, name) ".text.merlin." prefix "." _MERLIN_STR(name)

#define MERLIN_PROG_MVDP(name) \
	__attribute__((section(".text.merlin.mvdp." #name), used)) \
	int name

#define MERLIN_PROG_XDP_V(name) \
	__attribute__((section(".text.merlin.xdp_v." #name), used)) \
	int name

#define MERLIN_PROG_TC_V(name) \
	__attribute__((section(".text.merlin.tc_v." #name), used)) \
	int name

#define MERLIN_PROG_KPROBE_V(name) \
	__attribute__((section(".text.merlin.kprobe_v." #name), used)) \
	int name

#define MERLIN_PROG_TRACEPOINT_V(name) \
	__attribute__((section(".text.merlin.tracepoint_v." #name), used)) \
	int name

#define MERLIN_PROG_SOCKET_FILTER_V(name) \
	__attribute__((section(".text.merlin.sockfilt_v." #name), used)) \
	int name

#define MERLIN_PROG_PERF_EVENT_V(name) \
	__attribute__((section(".text.merlin.perfev_v." #name), used)) \
	int name

/* Map placement.  See <merlin/maps.h> MERLIN_MAP_DEF for the typical
 * usage; this macro is exposed for users wanting to declare custom
 * map records directly.
 */
#define MERLIN_MAPS_SECTION __attribute__((section(".merlin.maps"), used))

/* Meta record placement.  Programs MUST emit exactly one
 * merlin_meta_v1 in this section; the typical pattern is to use
 * MERLIN_PROFILE_LINUX_RV64 / MERLIN_PROFILE_RTOS_RV32 macros from
 * <merlin/merlin.h>.
 */
#define MERLIN_META_SECTION __attribute__((section(".merlin.meta"), used))

#endif /* _MERLIN_SECTION_MACROS_H */
