/* SPDX-License-Identifier: GPL-2.0 OR BSD-2-Clause */
/*
 * <merlin/types.h> - fixed-width integer types for MERLIN-V programs.
 *
 * MERLIN-V programs are compiled in -ffreestanding mode without
 * libc; we cannot pull <stdint.h> through to get __u32 et al on
 * every toolchain.  This header provides the project's canonical
 * typedefs in a way that works on both GCC and Clang.
 *
 * Programs that don't care about isolating from libc may simply
 * include <stdint.h> and use uint32_t etc. directly; the
 * MERLIN headers accept either spelling.
 */
#ifndef _MERLIN_TYPES_H
#define _MERLIN_TYPES_H

#if defined(__has_include)
# if __has_include(<stdint.h>)
#  include <stdint.h>
#  define _MERLIN_HAVE_STDINT 1
# endif
#endif

#ifdef _MERLIN_HAVE_STDINT
typedef int8_t   __merlin_s8;
typedef uint8_t  __merlin_u8;
typedef int16_t  __merlin_s16;
typedef uint16_t __merlin_u16;
typedef int32_t  __merlin_s32;
typedef uint32_t __merlin_u32;
typedef int64_t  __merlin_s64;
typedef uint64_t __merlin_u64;
#else
typedef signed char        __merlin_s8;
typedef unsigned char      __merlin_u8;
typedef signed short       __merlin_s16;
typedef unsigned short     __merlin_u16;
typedef signed int         __merlin_s32;
typedef unsigned int       __merlin_u32;
__extension__ typedef signed long long   __merlin_s64;
__extension__ typedef unsigned long long __merlin_u64;
#endif

/* Convenience aliases used inside MERLIN headers; mirror the kernel
 * UAPI spelling so structs declared here can be cross-referenced
 * against <linux/merlin.h>.
 */
typedef __merlin_s8   __s8;
typedef __merlin_u8   __u8;
typedef __merlin_s16  __s16;
typedef __merlin_u16  __u16;
typedef __merlin_s32  __s32;
typedef __merlin_u32  __u32;
typedef __merlin_s64  __s64;
typedef __merlin_u64  __u64;

#endif /* _MERLIN_TYPES_H */
