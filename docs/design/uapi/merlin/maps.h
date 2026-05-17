/* SPDX-License-Identifier: GPL-2.0 OR BSD-2-Clause */
/*
 * <merlin/maps.h> - map declaration macros for MERLIN-V programs.
 *
 * A MERLIN-V program describes its maps by placing one
 * merlin_map_desc_v1 record per map in the .merlin.maps section.
 * The on-disk record layout is fixed UAPI; see
 * docs/design/02-isa-and-bytecode.md §8.5.
 *
 * The MERLIN_MAP_DEF() macro emits the record and produces a
 * compile-time symbol of type "struct merlin_map *" that programs
 * use as the first argument to map-helper calls.  The actual
 * struct merlin_map is opaque; what the symbol holds is a
 * stable identifier the loader resolves at install time.
 *
 * Usage:
 *
 *     MERLIN_MAP_DEF(my_counters,
 *         .type        = MERLIN_MAP_TYPE_PERCPU_ARRAY,
 *         .key_size    = sizeof(__u32),
 *         .value_size  = sizeof(__u64),
 *         .max_entries = 256,
 *     );
 *
 *     __u64 *v = merlin_map_lookup_elem(&my_counters, &key);
 */
#ifndef _MERLIN_MAPS_H
#define _MERLIN_MAPS_H

#include <merlin/types.h>
#include <merlin/section_macros.h>

/*
 * Mirror of struct merlin_map_desc_v1 from
 * docs/design/02-isa-and-bytecode.md §8.5; the in-program record-
 * emitting layout matches the on-disk format the loader parses.
 *
 * Total size: 80 bytes per record.
 */
#define MERLIN_MAP_NAME_MAX  32

struct merlin_map_desc_v1 {
	char     name[MERLIN_MAP_NAME_MAX]; /* NUL-padded; loader-visible identity */
	__u32    type;                      /* enum merlin_map_type            */
	__u32    key_size;                  /* bytes                           */
	__u32    value_size;                /* bytes                           */
	__u32    max_entries;
	__u32    flags;                     /* MERLIN_MAP_F_*                  */
	__u32    key_btf_id;                /* index into .merlin.btf; 0 = none*/
	__u32    value_btf_id;              /* index into .merlin.btf; 0 = none*/
	__u32    inner_map_idx;             /* for map-in-map; 0xFFFFFFFFu = none */
	__u32    _reserved[4];              /* MBZ                             */
};

/* Map type constants - mirror enum merlin_map_type in <linux/merlin.h>.
 * Programs include this header standalone; we duplicate the constants
 * here rather than pull <linux/merlin.h> through into program builds.
 * The two headers are kept in sync by the source-of-truth rule
 * (see uapi/README.md).
 */
#ifndef MERLIN_MAP_TYPE_UNSPEC
#define MERLIN_MAP_TYPE_UNSPEC          0
#define MERLIN_MAP_TYPE_HASH            1
#define MERLIN_MAP_TYPE_ARRAY           2
#define MERLIN_MAP_TYPE_PERCPU_HASH     3
#define MERLIN_MAP_TYPE_PERCPU_ARRAY    4
#define MERLIN_MAP_TYPE_LRU_HASH        5
#define MERLIN_MAP_TYPE_LPM_TRIE        6
#define MERLIN_MAP_TYPE_RINGBUF         7
#define MERLIN_MAP_TYPE_PROG_ARRAY      8
#define MERLIN_MAP_TYPE_XSKMAP          9
#define MERLIN_MAP_TYPE_MVSKMAP         10
#endif

#ifndef MERLIN_MAP_F_NO_PREALLOC
#define MERLIN_MAP_F_NO_PREALLOC        (1U << 0)
#define MERLIN_MAP_F_NO_COMMON_LRU      (1U << 1)
#define MERLIN_MAP_F_NUMA_NODE          (1U << 2)
#define MERLIN_MAP_F_RDONLY             (1U << 3)
#define MERLIN_MAP_F_WRONLY             (1U << 4)
#define MERLIN_MAP_F_RDONLY_PROG        (1U << 7)
#define MERLIN_MAP_F_WRONLY_PROG        (1U << 8)
#define MERLIN_MAP_F_INNER_MAP          (1U << 12)
#endif

struct merlin_map; /* opaque; declared also in <merlin/helpers.h> */

/*
 * MERLIN_MAP_DEF(name, ...fields)
 *
 * Emits a merlin_map_desc_v1 record in .merlin.maps and a program-
 * visible "struct merlin_map *<name>" symbol.  The loader walks
 * the descriptors, creates the maps via MERLIN_MAP_CREATE, and
 * patches the program image so that &<name> resolves to the new
 * map fd (or kernel handle).
 *
 * Field order in the initialiser follows C99 designated-initialiser
 * syntax; only "type" is required, all others have sensible defaults.
 *
 * The .desc_size field is set automatically; the .name field is set
 * from the symbol name (truncated to MERLIN_OBJ_NAME_LEN).
 */
#define _MERLIN_STR2(s) #s
#define _MERLIN_STRINGIFY(s) _MERLIN_STR2(s)

#define MERLIN_MAP_DEF(_name, ...)                                            \
	MERLIN_MAPS_SECTION                                                   \
	const struct merlin_map_desc_v1 __merlin_map_desc_##_name = {         \
		.name = _MERLIN_STR2(_name),                                  \
		__VA_ARGS__                                                   \
	};                                                                    \
	extern struct merlin_map _name

/*
 * Inner-map declarations for map-in-map.  The inner map's record is
 * emitted with MERLIN_MAP_F_INNER_MAP set; the outer references it
 * by index into .merlin.maps.
 */
#define MERLIN_INNER_MAP_DEF(_name, ...)                                      \
	MERLIN_MAPS_SECTION                                                   \
	const struct merlin_map_desc_v1 __merlin_map_desc_##_name = {         \
		.name      = _MERLIN_STR2(_name),                             \
		.flags     = MERLIN_MAP_F_INNER_MAP,                          \
		__VA_ARGS__                                                   \
	};                                                                    \
	extern struct merlin_map _name

#endif /* _MERLIN_MAPS_H */
