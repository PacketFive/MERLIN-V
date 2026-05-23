// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * decode.c — per-event attribute decoders for merlin-watcher.
 *
 * Each decoder uses libnl's nla_parse to extract the attributes
 * defined in docs/design/14-mvcp-multicast.md §3 and prints either
 * a human-readable line or a JSON-Lines record.
 */
#include <stdio.h>
#include <stdint.h>
#include <inttypes.h>
#include <string.h>
#include <time.h>

#include <netlink/netlink.h>
#include <netlink/genl/genl.h>
#include <netlink/attr.h>

#include "merlin_watcher.h"

/* ------------------------------------------------------------------ */
static void print_tag(const uint8_t *tag, char *out, size_t cap)
{
snprintf(out, cap,
 "%02x%02x%02x%02x%02x%02x%02x%02x",
 tag[0], tag[1], tag[2], tag[3],
 tag[4], tag[5], tag[6], tag[7]);
}

static const char *action_name(uint8_t a)
{
switch (a) {
case MERLIN_NL_PROG_LOADED:    return "loaded";
case MERLIN_NL_PROG_ATTACHED:  return "attached";
case MERLIN_NL_PROG_DETACHED:  return "detached";
case MERLIN_NL_PROG_UNLOADED:  return "unloaded";
default:                        return "?";
}
}

static const char *map_op_name(uint8_t op)
{
switch (op) {
case MERLIN_NL_MAP_OP_INSERT:      return "insert";
case MERLIN_NL_MAP_OP_UPDATE:      return "update";
case MERLIN_NL_MAP_OP_DELETE:      return "delete";
case MERLIN_NL_MAP_OP_REPLACE_ALL: return "replace_all";
default:                            return "?";
}
}

/* ------------------------------------------------------------------ */
int decode_prog_lifecycle(struct nl_msg *msg, int as_json)
{
struct nlattr *attrs[MERLIN_NLATTR_MAX + 1];
int rc = genlmsg_parse(nlmsg_hdr(msg), 0, attrs,
       MERLIN_NLATTR_MAX, NULL);
if (rc < 0) return NL_OK;

uint32_t ns      = attrs[MERLIN_NLATTR_NS_ID]    ? nla_get_u32(attrs[MERLIN_NLATTR_NS_ID])    : 0;
uint32_t prog    = attrs[MERLIN_NLATTR_PROG_ID]  ? nla_get_u32(attrs[MERLIN_NLATTR_PROG_ID])  : 0;
uint8_t  action  = attrs[MERLIN_NLATTR_ACTION]   ? nla_get_u8 (attrs[MERLIN_NLATTR_ACTION])   : 0;
uint64_t time_ns = attrs[MERLIN_NLATTR_TIME_NS]  ? nla_get_u64(attrs[MERLIN_NLATTR_TIME_NS])  : 0;
char tagstr[20] = "?";
if (attrs[MERLIN_NLATTR_TAG] && nla_len(attrs[MERLIN_NLATTR_TAG]) == 8)
print_tag(nla_data(attrs[MERLIN_NLATTR_TAG]), tagstr, sizeof(tagstr));

if (as_json) {
printf("{\"ts\":%" PRIu64 ",\"ev\":\"prog_lifecycle\","
       "\"ns\":%u,\"prog\":%u,\"act\":\"%s\",\"tag\":\"%s\"}\n",
       time_ns, ns, prog, action_name(action), tagstr);
} else {
printf("[%" PRIu64 "] prog_lifecycle ns=%u prog=%u act=%s tag=%s\n",
       time_ns, ns, prog, action_name(action), tagstr);
}
fflush(stdout);
return NL_OK;
}

/* ------------------------------------------------------------------ */
int decode_map_update(struct nl_msg *msg, int as_json)
{
struct nlattr *attrs[MERLIN_NLATTR_MAX + 1];
int rc = genlmsg_parse(nlmsg_hdr(msg), 0, attrs,
       MERLIN_NLATTR_MAX, NULL);
if (rc < 0) return NL_OK;

uint32_t ns     = attrs[MERLIN_NLATTR_NS_ID]    ? nla_get_u32(attrs[MERLIN_NLATTR_NS_ID])    : 0;
uint32_t mapid  = attrs[MERLIN_NLATTR_MAP_ID]   ? nla_get_u32(attrs[MERLIN_NLATTR_MAP_ID])   : 0;
uint64_t txn    = attrs[MERLIN_NLATTR_TXN_ID]   ? nla_get_u64(attrs[MERLIN_NLATTR_TXN_ID])   : 0;
uint8_t  op     = attrs[MERLIN_NLATTR_OP]       ? nla_get_u8 (attrs[MERLIN_NLATTR_OP])       : 0;
uint64_t time_ns= attrs[MERLIN_NLATTR_TIME_NS]  ? nla_get_u64(attrs[MERLIN_NLATTR_TIME_NS])  : 0;

if (as_json) {
printf("{\"ts\":%" PRIu64 ",\"ev\":\"map_update\","
       "\"ns\":%u,\"map\":%u,\"txn\":%" PRIu64 ",\"op\":\"%s\"}\n",
       time_ns, ns, mapid, txn, map_op_name(op));
} else {
printf("[%" PRIu64 "] map_update ns=%u map=%u txn=%" PRIu64 " op=%s\n",
       time_ns, ns, mapid, txn, map_op_name(op));
}
fflush(stdout);
return NL_OK;
}

/* ------------------------------------------------------------------ */
int decode_telemetry(struct nl_msg *msg, int as_json)
{
struct nlattr *attrs[MERLIN_NLATTR_MAX + 1];
int rc = genlmsg_parse(nlmsg_hdr(msg), 0, attrs,
       MERLIN_NLATTR_MAX, NULL);
if (rc < 0) return NL_OK;

uint32_t ns       = attrs[MERLIN_NLATTR_NS_ID]    ? nla_get_u32(attrs[MERLIN_NLATTR_NS_ID])    : 0;
uint32_t prog     = attrs[MERLIN_NLATTR_PROG_ID]  ? nla_get_u32(attrs[MERLIN_NLATTR_PROG_ID])  : 0;
uint64_t time_ns  = attrs[MERLIN_NLATTR_TIME_NS]  ? nla_get_u64(attrs[MERLIN_NLATTR_TIME_NS])  : 0;
uint64_t run_cnt  = attrs[MERLIN_NLATTR_RUN_CNT]  ? nla_get_u64(attrs[MERLIN_NLATTR_RUN_CNT])  : 0;
uint64_t run_time = attrs[MERLIN_NLATTR_RUN_TIME_NS] ? nla_get_u64(attrs[MERLIN_NLATTR_RUN_TIME_NS]) : 0;

if (as_json) {
printf("{\"ts\":%" PRIu64 ",\"ev\":\"telemetry\","
       "\"ns\":%u,\"prog\":%u,"
       "\"run_cnt\":%" PRIu64 ",\"run_time_ns\":%" PRIu64 "}\n",
       time_ns, ns, prog, run_cnt, run_time);
} else {
printf("[%" PRIu64 "] telemetry ns=%u prog=%u run_cnt=%" PRIu64
       " run_time=%" PRIu64 " ns\n",
       time_ns, ns, prog, run_cnt, run_time);
}
fflush(stdout);
return NL_OK;
}

/* ------------------------------------------------------------------ */
int decode_namespace(struct nl_msg *msg, int as_json)
{
struct nlattr *attrs[MERLIN_NLATTR_MAX + 1];
int rc = genlmsg_parse(nlmsg_hdr(msg), 0, attrs,
       MERLIN_NLATTR_MAX, NULL);
if (rc < 0) return NL_OK;

uint32_t ns      = attrs[MERLIN_NLATTR_NS_ID]    ? nla_get_u32(attrs[MERLIN_NLATTR_NS_ID])    : 0;
uint8_t  action  = attrs[MERLIN_NLATTR_ACTION]   ? nla_get_u8 (attrs[MERLIN_NLATTR_ACTION])   : 0;
uint64_t time_ns = attrs[MERLIN_NLATTR_TIME_NS]  ? nla_get_u64(attrs[MERLIN_NLATTR_TIME_NS])  : 0;
const char *act = (action == MERLIN_NL_NS_CREATED) ? "created" :
  (action == MERLIN_NL_NS_DESTROYED) ? "destroyed" : "?";

if (as_json)
printf("{\"ts\":%" PRIu64 ",\"ev\":\"namespace\","
       "\"ns\":%u,\"act\":\"%s\"}\n", time_ns, ns, act);
else
printf("[%" PRIu64 "] namespace ns=%u act=%s\n",
       time_ns, ns, act);
fflush(stdout);
return NL_OK;
}

/* ------------------------------------------------------------------ */
int decode_attestation(struct nl_msg *msg, int as_json)
{
struct nlattr *attrs[MERLIN_NLATTR_MAX + 1];
int rc = genlmsg_parse(nlmsg_hdr(msg), 0, attrs,
       MERLIN_NLATTR_MAX, NULL);
if (rc < 0) return NL_OK;

struct genlmsghdr *gnlh = nlmsg_data(nlmsg_hdr(msg));
int is_req = (gnlh->cmd == MERLIN_NL_EV_ATTESTATION_REQ);

uint32_t target  = attrs[MERLIN_NLATTR_TARGET_PROG_ID]
   ? nla_get_u32(attrs[MERLIN_NLATTR_TARGET_PROG_ID]) : 0;
uint64_t time_ns = attrs[MERLIN_NLATTR_TIME_NS]
   ? nla_get_u64(attrs[MERLIN_NLATTR_TIME_NS]) : 0;
int quote_len   = attrs[MERLIN_NLATTR_QUOTE]
  ? nla_len(attrs[MERLIN_NLATTR_QUOTE]) : 0;

if (as_json)
printf("{\"ts\":%" PRIu64 ",\"ev\":\"attestation_%s\","
       "\"target\":%u,\"quote_len\":%d}\n",
       time_ns, is_req ? "req" : "resp", target, quote_len);
else
printf("[%" PRIu64 "] attestation_%s target=%u quote=%dB\n",
       time_ns, is_req ? "req" : "resp", target, quote_len);
fflush(stdout);
return NL_OK;
}

/* ------------------------------------------------------------------ */
int decode_reject(struct nl_msg *msg, int as_json)
{
struct nlattr *attrs[MERLIN_NLATTR_MAX + 1];
int rc = genlmsg_parse(nlmsg_hdr(msg), 0, attrs,
       MERLIN_NLATTR_MAX, NULL);
if (rc < 0) return NL_OK;

uint32_t ns      = attrs[MERLIN_NLATTR_NS_ID]    ? nla_get_u32(attrs[MERLIN_NLATTR_NS_ID])    : 0;
uint32_t pc      = attrs[MERLIN_NLATTR_REJECT_PC] ? nla_get_u32(attrs[MERLIN_NLATTR_REJECT_PC]) : 0;
uint64_t time_ns = attrs[MERLIN_NLATTR_TIME_NS]  ? nla_get_u64(attrs[MERLIN_NLATTR_TIME_NS])  : 0;
const char *reason = "";
if (attrs[MERLIN_NLATTR_REJECT_REASON])
reason = nla_get_string(attrs[MERLIN_NLATTR_REJECT_REASON]);

if (as_json)
printf("{\"ts\":%" PRIu64 ",\"ev\":\"verifier_reject\","
       "\"ns\":%u,\"pc\":%u,\"reason\":\"%s\"}\n",
       time_ns, ns, pc, reason);
else
printf("[%" PRIu64 "] verifier_reject ns=%u pc=%u: %s\n",
       time_ns, ns, pc, reason);
fflush(stdout);
return NL_OK;
}

/* ------------------------------------------------------------------ */
int decode_ratelimit_drop(struct nl_msg *msg, int as_json)
{
struct nlattr *attrs[MERLIN_NLATTR_MAX + 1];
int rc = genlmsg_parse(nlmsg_hdr(msg), 0, attrs,
       MERLIN_NLATTR_MAX, NULL);
if (rc < 0) return NL_OK;

uint32_t cls    = attrs[MERLIN_NLATTR_RL_CLASS] ? nla_get_u32(attrs[MERLIN_NLATTR_RL_CLASS]) : 0;
uint64_t drops  = attrs[MERLIN_NLATTR_RL_DROPS] ? nla_get_u64(attrs[MERLIN_NLATTR_RL_DROPS]) : 0;
uint64_t time_ns= attrs[MERLIN_NLATTR_TIME_NS]  ? nla_get_u64(attrs[MERLIN_NLATTR_TIME_NS])  : 0;

if (as_json)
printf("{\"ts\":%" PRIu64 ",\"ev\":\"ratelimit_drop\","
       "\"class\":%u,\"drops\":%" PRIu64 "}\n",
       time_ns, cls, drops);
else
printf("[%" PRIu64 "] ratelimit_drop class=%u drops=%" PRIu64 "\n",
       time_ns, cls, drops);
fflush(stdout);
return NL_OK;
}
