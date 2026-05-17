/* SPDX-License-Identifier: GPL-2.0 OR BSD-2-Clause */
/*
 * <merlin/license.h> - SPDX licence declaration for MERLIN-V programs.
 *
 * Every MERLIN-V program must emit exactly one NUL-terminated SPDX
 * licence identifier in the .merlin.license section.  The loader
 * matches against a kernel-side allowlist (typically "GPL", "GPL-2.0",
 * "GPL-2.0-only", "Dual BSD/GPL", etc. - identical to the existing
 * eBPF licence acceptance).
 *
 * Usage:
 *
 *    #include <merlin/merlin.h>
 *    MERLIN_LICENSE("GPL");
 */
#ifndef _MERLIN_LICENSE_H
#define _MERLIN_LICENSE_H

#define MERLIN_LICENSE(lic)                                            \
	__attribute__((section(".merlin.license"), used))              \
	const char __merlin_license[] = (lic)

#endif /* _MERLIN_LICENSE_H */
