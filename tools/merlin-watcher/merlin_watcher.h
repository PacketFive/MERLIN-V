/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * merlin_watcher.h — Genl family / attribute / command IDs for the
 * MERLIN-V multicast channel.  This file mirrors the in-kernel
 * uapi/linux/merlin_nl.h that will ship with the kernel module
 * post-upstream-merge.
 *
 * Cross-reference: docs/design/14-mvcp-multicast.md
 */
#ifndef MERLIN_WATCHER_H
#define MERLIN_WATCHER_H

#include <stdint.h>

#define MERLIN_NL_FAMILY_NAME    "merlin_nl"
#define MERLIN_NL_FAMILY_VERSION 1
#define MERLIN_NL_MCGRP_EVENTS   "events"

/* Generic Netlink command IDs (cmd in genlmsghdr). */
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
#define MERLIN_NL_EV_MAX (__MERLIN_NL_EV_MAX - 1)

/* Attribute IDs. */
enum {
MERLIN_NLATTR_UNSPEC              = 0,
MERLIN_NLATTR_NS_ID               = 1,
MERLIN_NLATTR_PROG_ID             = 2,
MERLIN_NLATTR_ACTION              = 3,
MERLIN_NLATTR_TAG                 = 4,
MERLIN_NLATTR_TIME_NS             = 5,
MERLIN_NLATTR_PROG_NAME           = 6,
MERLIN_NLATTR_MAP_ID              = 7,
MERLIN_NLATTR_TXN_ID              = 8,
MERLIN_NLATTR_OP                  = 9,
MERLIN_NLATTR_KEY_HASH            = 10,
MERLIN_NLATTR_VALUE_HASH          = 11,
MERLIN_NLATTR_RUN_CNT             = 12,
MERLIN_NLATTR_RUN_TIME_NS         = 13,
MERLIN_NLATTR_RECURSION_MISSES    = 14,
MERLIN_NLATTR_PKT_DROP            = 15,
MERLIN_NLATTR_PKT_PASS            = 16,
MERLIN_NLATTR_NONCE               = 17,
MERLIN_NLATTR_TARGET_PROG_ID      = 18,
MERLIN_NLATTR_QUOTE               = 19,
MERLIN_NLATTR_QUOTE_SIG_ALG       = 20,
MERLIN_NLATTR_REJECT_REASON       = 21,
MERLIN_NLATTR_REJECT_PC           = 22,
MERLIN_NLATTR_NSFS_PATH           = 23,
MERLIN_NLATTR_RL_CLASS            = 24,
MERLIN_NLATTR_RL_DROPS            = 25,
__MERLIN_NLATTR_MAX,
};
#define MERLIN_NLATTR_MAX (__MERLIN_NLATTR_MAX - 1)

/* Action enums (for ACTION attribute) */
#define MERLIN_NL_PROG_LOADED    1
#define MERLIN_NL_PROG_ATTACHED  2
#define MERLIN_NL_PROG_DETACHED  3
#define MERLIN_NL_PROG_UNLOADED  4

#define MERLIN_NL_NS_CREATED     1
#define MERLIN_NL_NS_DESTROYED   2

#define MERLIN_NL_MAP_OP_INSERT      1
#define MERLIN_NL_MAP_OP_UPDATE      2
#define MERLIN_NL_MAP_OP_DELETE      3
#define MERLIN_NL_MAP_OP_REPLACE_ALL 4

/* Decoders (defined in decode.c). */
struct nl_msg;
int decode_prog_lifecycle (struct nl_msg *msg, int as_json);
int decode_map_update     (struct nl_msg *msg, int as_json);
int decode_telemetry      (struct nl_msg *msg, int as_json);
int decode_namespace      (struct nl_msg *msg, int as_json);
int decode_attestation    (struct nl_msg *msg, int as_json);
int decode_reject         (struct nl_msg *msg, int as_json);
int decode_ratelimit_drop (struct nl_msg *msg, int as_json);

#endif /* MERLIN_WATCHER_H */
