// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * stub_genl.c — out-of-tree kernel module that registers a stub
 * 'merlin_nl' Generic Netlink family and emits synthetic events on
 * a 1-second timer.  Lets us shake out tools/merlin-watcher/ without
 * waiting for the in-tree kernel/merlin/ module to ship.
 *
 * Compile + load:
 *   $ make
 *   $ sudo insmod stub_genl.ko
 *
 * Then in another shell:
 *   $ cd ..
 *   $ make
 *   $ sudo ./merlin-watcher
 *   [merlin-watcher] subscribed to 'merlin_nl/events' (family=N, group=M)
 *   [1731234567000000] prog_lifecycle ns=0 prog=1 act=loaded tag=deadbeef01020304
 *   [1731234568000000] telemetry ns=0 prog=1 run_cnt=N run_time=M ns
 *   ...
 *
 * This is a *minimal* producer; it does not implement the full
 * 8-event-class set.  Just two events (prog_lifecycle + telemetry)
 * cycling on a timer.  Enough to confirm the user-space subscription,
 * decoding, and JSON output paths work.
 */

#include <linux/module.h>
#include <linux/timer.h>
#include <linux/jiffies.h>
#include <linux/ktime.h>
#include <net/genetlink.h>
#include <net/netlink.h>

#define MERLIN_NL_FAMILY_NAME    "merlin_nl"
#define MERLIN_NL_FAMILY_VERSION 1
#define MERLIN_NL_MCGRP_EVENTS   "events"

enum {
MERLIN_NL_EV_UNSPEC               = 0,
MERLIN_NL_EV_PROG_LIFECYCLE       = 1,
MERLIN_NL_EV_MAP_UPDATE           = 2,
MERLIN_NL_EV_TELEMETRY_SAMPLE     = 3,
MERLIN_NL_EV_NAMESPACE            = 4,
MERLIN_NL_EV_ATTESTATION_REQ      = 5,
MERLIN_NL_EV_ATTESTATION_RESP     = 6,
MERLIN_NL_EV_VERIFIER_REJECT      = 7,
MERLIN_NL_EV_RATELIMIT_DROP       = 8,
__MERLIN_NL_EV_MAX,
};

enum {
MERLIN_NLATTR_UNSPEC              = 0,
MERLIN_NLATTR_NS_ID               = 1,
MERLIN_NLATTR_PROG_ID             = 2,
MERLIN_NLATTR_ACTION              = 3,
MERLIN_NLATTR_TAG                 = 4,
MERLIN_NLATTR_TIME_NS             = 5,
MERLIN_NLATTR_RUN_CNT             = 12,
MERLIN_NLATTR_RUN_TIME_NS         = 13,
__MERLIN_NLATTR_MAX,
};
#define MERLIN_NLATTR_MAX (__MERLIN_NLATTR_MAX - 1)

static const struct nla_policy merlin_nl_policy[__MERLIN_NLATTR_MAX] = {
[MERLIN_NLATTR_NS_ID]    = { .type = NLA_U32 },
[MERLIN_NLATTR_PROG_ID]  = { .type = NLA_U32 },
[MERLIN_NLATTR_ACTION]   = { .type = NLA_U8  },
[MERLIN_NLATTR_TAG]      = { .type = NLA_BINARY, .len = 8 },
[MERLIN_NLATTR_TIME_NS]  = { .type = NLA_U64 },
[MERLIN_NLATTR_RUN_CNT]  = { .type = NLA_U64 },
[MERLIN_NLATTR_RUN_TIME_NS] = { .type = NLA_U64 },
};

/* Multicast group */
static const struct genl_multicast_group merlin_nl_mcgrps[] = {
{ .name = MERLIN_NL_MCGRP_EVENTS },
};

/* The family (registered with no commands; we only multicast) */
static struct genl_family merlin_nl_family = {
.name     = MERLIN_NL_FAMILY_NAME,
.version  = MERLIN_NL_FAMILY_VERSION,
.maxattr  = MERLIN_NLATTR_MAX,
.policy   = merlin_nl_policy,
.netnsok  = false,
.module   = THIS_MODULE,
.ops      = NULL,
.n_ops    = 0,
.mcgrps   = merlin_nl_mcgrps,
.n_mcgrps = ARRAY_SIZE(merlin_nl_mcgrps),
};

static u32 fake_prog_id = 1;
static u64 fake_run_cnt;

static void emit_prog_lifecycle(void)
{
struct sk_buff *skb;
void *hdr;
u8 fake_tag[8] = { 0xde, 0xad, 0xbe, 0xef, 1, 2, 3, 4 };

skb = genlmsg_new(NLMSG_GOODSIZE, GFP_KERNEL);
if (!skb) return;

hdr = genlmsg_put(skb, 0, 0, &merlin_nl_family, 0,
  MERLIN_NL_EV_PROG_LIFECYCLE);
if (!hdr) { nlmsg_free(skb); return; }

if (nla_put_u32(skb, MERLIN_NLATTR_NS_ID, 0)              ||
    nla_put_u32(skb, MERLIN_NLATTR_PROG_ID, fake_prog_id) ||
    nla_put_u8 (skb, MERLIN_NLATTR_ACTION, 1)             ||
    nla_put    (skb, MERLIN_NLATTR_TAG, 8, fake_tag)      ||
    nla_put_u64_64bit(skb, MERLIN_NLATTR_TIME_NS,
      ktime_get_boottime_ns(), MERLIN_NLATTR_UNSPEC)) {
genlmsg_cancel(skb, hdr);
nlmsg_free(skb);
return;
}
genlmsg_end(skb, hdr);
genlmsg_multicast(&merlin_nl_family, skb, 0, 0, GFP_KERNEL);
}

static void emit_telemetry(void)
{
struct sk_buff *skb;
void *hdr;

skb = genlmsg_new(NLMSG_GOODSIZE, GFP_KERNEL);
if (!skb) return;

hdr = genlmsg_put(skb, 0, 0, &merlin_nl_family, 0,
  MERLIN_NL_EV_TELEMETRY_SAMPLE);
if (!hdr) { nlmsg_free(skb); return; }

fake_run_cnt += 1024;
if (nla_put_u32(skb, MERLIN_NLATTR_NS_ID, 0)              ||
    nla_put_u32(skb, MERLIN_NLATTR_PROG_ID, fake_prog_id) ||
    nla_put_u64_64bit(skb, MERLIN_NLATTR_TIME_NS,
      ktime_get_boottime_ns(), MERLIN_NLATTR_UNSPEC) ||
    nla_put_u64_64bit(skb, MERLIN_NLATTR_RUN_CNT, fake_run_cnt,
      MERLIN_NLATTR_UNSPEC)                          ||
    nla_put_u64_64bit(skb, MERLIN_NLATTR_RUN_TIME_NS,
      fake_run_cnt * 100, MERLIN_NLATTR_UNSPEC)) {
genlmsg_cancel(skb, hdr);
nlmsg_free(skb);
return;
}
genlmsg_end(skb, hdr);
genlmsg_multicast(&merlin_nl_family, skb, 0, 0, GFP_KERNEL);
}

static struct timer_list stub_timer;
static int counter;

static void stub_tick(struct timer_list *t)
{
counter++;
if (counter == 1)
emit_prog_lifecycle();
emit_telemetry();
mod_timer(&stub_timer, jiffies + HZ);
}

static int __init stub_init(void)
{
int rc = genl_register_family(&merlin_nl_family);
if (rc) {
pr_err("stub_genl: genl_register_family: %d\n", rc);
return rc;
}
pr_info("stub_genl: registered '%s'; emitting test events every 1 s\n",
MERLIN_NL_FAMILY_NAME);

timer_setup(&stub_timer, stub_tick, 0);
mod_timer(&stub_timer, jiffies + HZ);
return 0;
}

static void __exit stub_exit(void)
{
del_timer_sync(&stub_timer);
genl_unregister_family(&merlin_nl_family);
pr_info("stub_genl: unloaded\n");
}

module_init(stub_init);
module_exit(stub_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("PacketFive");
MODULE_DESCRIPTION("Stub Genl family for merlin-watcher development");
