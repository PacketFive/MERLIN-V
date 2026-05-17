/* SPDX-License-Identifier: GPL-2.0 */
/*
 * drop_all.c - minimal MVDP program: increment a counter, drop the packet.
 *
 * This is the canonical "hello world" of MERLIN-V programs.  It exercises
 * every program-side header:
 *
 *   <merlin/merlin.h>    umbrella
 *   <merlin/mvdp.h>      MVDP ctx, verdicts, helper wrappers
 *
 * Build (RV64 target):
 *
 *   riscv64-linux-gnu-gcc \
 *       -I ../uapi \
 *       -O2 -ffreestanding -fno-stack-protector \
 *       -fno-asynchronous-unwind-tables -fno-builtin -fno-jump-tables \
 *       -fno-plt -nostdlib \
 *       -mabi=lp64 -march=rv64imac_zicsr_zifencei -mcmodel=medany \
 *       -c drop_all.c -o drop_all.merlin.o
 *
 * Verify (any host):
 *
 *   gcc -I ../uapi -ffreestanding -fno-builtin -nostdlib \
 *       -c drop_all.c -o /dev/null
 *
 * The host build does not produce a runnable object (helper calls
 * resolve to an extern stub) but it does compile-test the headers,
 * which is useful in CI before a full cross toolchain is available.
 */
#include <merlin/merlin.h>
#include <merlin/mvdp.h>

MERLIN_LICENSE("GPL");
MERLIN_META(MERLIN_PROFILE_LINUX_RV64);

MERLIN_MAP_DEF(packet_counter,
	.type        = MERLIN_MAP_TYPE_PERCPU_ARRAY,
	.key_size    = sizeof(__u32),
	.value_size  = sizeof(__u64),
	.max_entries = 1,
);

MERLIN_PROG_MVDP(drop_all)(struct mvdp_md *ctx)
{
	__u32 key = 0;
	__u64 *cnt;

	(void)ctx;

	cnt = merlin_map_lookup_elem(&packet_counter, &key);
	if (cnt)
		*cnt += 1;

	return MVDP_DROP;
}
