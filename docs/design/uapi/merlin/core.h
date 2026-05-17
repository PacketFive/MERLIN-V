/* SPDX-License-Identifier: GPL-2.0 OR BSD-2-Clause */
/*
 * <merlin/core.h> - CO-RE-V (Compile Once, Run Everywhere - V)
 * relocation intrinsics.
 *
 * MERLIN-V programs reference kernel/firmware data types whose
 * memory layout may differ between the build host and the running
 * target (different struct field orderings, different padding,
 * different ABI bit widths).  CO-RE-V solves this by recording the
 * field reference in a structured relocation; the loader resolves
 * the relocation against the live target's MERLIN BTF and patches
 * the instruction immediate that encodes the offset.
 *
 * Canonical spec: docs/design/04-toolchain.md §4 and
 * docs/design/02-isa-and-bytecode.md §8.6.2.
 *
 * The intrinsics below are the user-visible interface.  Each emits
 * a load against a tagged section that the compiler keeps; the
 * objtool/loader pipeline turns those tags into MERLIN_RELOC_CORE_*
 * records in .merlin.relocs.
 */
#ifndef _MERLIN_CORE_H
#define _MERLIN_CORE_H

#include <merlin/types.h>

/*
 * Access-kind values mirror the CO-RE access kinds in
 * 02-isa-and-bytecode.md §8.6.2:
 *
 *    0    FIELD_BYTE_OFFSET     byte offset of a field
 *    1    FIELD_BYTE_SIZE       size in bytes of a field
 *    2    FIELD_EXISTS          1 if field exists in target, else 0
 *    3    FIELD_SIGNED          1 if field is a signed integer
 *    4    FIELD_LSHIFT_U64      shift-amount for bitfield extraction
 *    5    FIELD_RSHIFT_U64      shift-amount for bitfield extraction
 *    6    TYPE_ID_LOCAL         BTF type id in program's own BTF
 *    7    TYPE_ID_TARGET        BTF type id in target's BTF
 *    8    TYPE_EXISTS           1 if type exists in target
 *    9    TYPE_SIZE             size in bytes of a type
 *    10   ENUMVAL_EXISTS        1 if enum value exists in target
 *    11   ENUMVAL_VALUE         resolved value of an enum constant
 */
enum merlin_core_access_kind {
	MERLIN_CORE_FIELD_BYTE_OFFSET = 0,
	MERLIN_CORE_FIELD_BYTE_SIZE   = 1,
	MERLIN_CORE_FIELD_EXISTS      = 2,
	MERLIN_CORE_FIELD_SIGNED      = 3,
	MERLIN_CORE_FIELD_LSHIFT_U64  = 4,
	MERLIN_CORE_FIELD_RSHIFT_U64  = 5,
	MERLIN_CORE_TYPE_ID_LOCAL     = 6,
	MERLIN_CORE_TYPE_ID_TARGET    = 7,
	MERLIN_CORE_TYPE_EXISTS       = 8,
	MERLIN_CORE_TYPE_SIZE         = 9,
	MERLIN_CORE_ENUMVAL_EXISTS    = 10,
	MERLIN_CORE_ENUMVAL_VALUE     = 11,
};

/*
 * The CO-RE-V intrinsic.  The compiler emits a load against a
 * tagged offset; the offset itself is meaningless until the loader
 * patches it.  __builtin_preserve_access_index is a clang built-in
 * (libbpf-style) - on GCC we provide an equivalent via a section
 * marker (see docs/design/04-toolchain.md §6 open item for the
 * GCC plumbing once finalised).
 */

#ifdef __clang__

#define MERLIN_CORE_READ(dst, src)                                            \
	do {                                                                  \
		__builtin_memcpy_inline(                                      \
			&(dst),                                               \
			__builtin_preserve_access_index(&(src)),              \
			sizeof(dst));                                         \
	} while (0)

#define MERLIN_CORE_READ_INTO(dst, src, fields...)                            \
	MERLIN_CORE_READ(dst, src.fields)

#define MERLIN_CORE_FIELD_OFFSET(field)                                       \
	__builtin_preserve_field_info((field), MERLIN_CORE_FIELD_BYTE_OFFSET)

#define MERLIN_CORE_FIELD_EXISTS(field)                                       \
	__builtin_preserve_field_info((field), MERLIN_CORE_FIELD_EXISTS)

#define MERLIN_CORE_TYPE_EXISTS(t)                                            \
	__builtin_preserve_type_info((*(t *)0), MERLIN_CORE_TYPE_EXISTS)

#define MERLIN_CORE_ENUM_VALUE(e, v)                                          \
	__builtin_preserve_enum_value((*(typeof(e) *)0), v,                   \
				      MERLIN_CORE_ENUMVAL_VALUE)

#else /* !__clang__ - GCC path */

/*
 * GCC equivalent.  GCC does not have __builtin_preserve_access_index;
 * the MERLIN-V GCC path uses an objtool-marker section described in
 * docs/design/12-core-v-spec.md §6:
 *
 *   1. The natural C access expression compiles normally - GCC emits
 *      the lw/ld/addi against the local-BTF offset.
 *   2. A static const string in .merlin.core_v_pending names the
 *      access symbolically; merlin-objtool post-processes the section
 *      and emits real R_MERLIN_CORE_* records in .merlin.relocs +
 *      paired records in .merlin.btf_ext, then deletes the pending
 *      section from the output object.
 *
 * The marker format is "<kind>:<placeholder>:<access_chain>:<kind_name>".
 * The objtool pass resolves placeholders (it has BTF and DWARF
 * available; the C source-level macros only know the access chain
 * lexically, not the stem type name).
 *
 * For uniqueness across translation units the marker uses
 * __COUNTER__; objtool finds the marker by walking the section in
 * file-emission order and pairs it with the next access instruction.
 */

#define _MERLIN_CONCAT2(a, b) a##b
#define _MERLIN_CONCAT(a, b)  _MERLIN_CONCAT2(a, b)
#define _MERLIN_CORE_MARKER_UNIQ() _MERLIN_CONCAT(__merlin_core_marker_, __COUNTER__)

#define _MERLIN_CORE_EMIT_MARKER(s)                                           \
	do {                                                                  \
		static const char _MERLIN_CONCAT(__merlin_core_marker_,       \
						 __LINE__)[]                  \
			__attribute__((section(".merlin.core_v_pending"),     \
				       used)) = s;                            \
		__asm__ __volatile__("# merlin-core-marker %0"                \
			: : "i"(&_MERLIN_CONCAT(__merlin_core_marker_,        \
						__LINE__)));                  \
	} while (0)

#define MERLIN_CORE_READ(dst, src)                                            \
	do {                                                                  \
		_MERLIN_CORE_EMIT_MARKER("F:?:0:BYTE_OFFSET");                \
		__builtin_memcpy(&(dst), &(src), sizeof(dst));                \
	} while (0)

#define MERLIN_CORE_READ_INTO(dst, src, fields...)                            \
	MERLIN_CORE_READ(dst, src.fields)

#define MERLIN_CORE_FIELD_OFFSET(field)                                       \
	((__u32)((char *)&(field) - (char *)0))

#define MERLIN_CORE_FIELD_EXISTS(field)  (1u)
#define MERLIN_CORE_TYPE_EXISTS(t)       (1u)
#define MERLIN_CORE_ENUM_VALUE(e, v)     ((__u64)(v))

#endif /* __clang__ */

#endif /* _MERLIN_CORE_H */
