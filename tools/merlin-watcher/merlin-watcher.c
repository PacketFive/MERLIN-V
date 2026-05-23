// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * merlin-watcher: user-space subscriber to the merlin_nl/events
 * multicast group.  See docs/design/14-mvcp-multicast.md for the
 * protocol specification.
 *
 * Design notes:
 *
 *  - Uses libnl-3 + libnl-genl-3 (Debian packages
 *    libnl-3-dev + libnl-genl-3-dev).
 *  - One socket; one multicast subscription; one event loop.
 *  - Per-event decoders in decode.c.
 *
 * STATUS: design-level prototype.  The Genl family it tries to
 * subscribe to is registered by either (a) the future in-tree
 * kernel/merlin/netlink.c (post-upstream merge) or
 * (b) test/stub_genl.ko in this directory.
 */
#define _GNU_SOURCE

#include <errno.h>
#include <getopt.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <netlink/netlink.h>
#include <netlink/genl/genl.h>
#include <netlink/genl/ctrl.h>

#include "merlin_watcher.h"

static struct nl_sock *g_sock;
static bool g_json;
static unsigned g_filter_mask = 0x1fe; /* bits 1..8 set: all event classes */

static const char *class_names[] = {
[MERLIN_NL_EV_PROG_LIFECYCLE]   = "prog_lifecycle",
[MERLIN_NL_EV_MAP_UPDATE]       = "map_update",
[MERLIN_NL_EV_TELEMETRY_SAMPLE] = "telemetry",
[MERLIN_NL_EV_NAMESPACE]        = "namespace",
[MERLIN_NL_EV_ATTESTATION_REQ]  = "attestation_req",
[MERLIN_NL_EV_ATTESTATION_RESP] = "attestation_resp",
[MERLIN_NL_EV_VERIFIER_REJECT]  = "reject",
[MERLIN_NL_EV_RATELIMIT_DROP]   = "ratelimit_drop",
};

static unsigned parse_filter(const char *spec)
{
unsigned mask = 0;
char *copy = strdup(spec);
char *tok = strtok(copy, ",");
while (tok) {
bool matched = false;
for (unsigned i = 1; i <= MERLIN_NL_EV_MAX; i++) {
if (class_names[i] && !strcmp(tok, class_names[i])) {
mask |= (1u << i);
matched = true;
break;
}
}
if (!matched)
fprintf(stderr,
"merlin-watcher: unknown filter class '%s'\n",
tok);
tok = strtok(NULL, ",");
}
free(copy);
return mask ? mask : 0x1fe;
}

static int on_event(struct nl_msg *msg, void *arg)
{
struct nlmsghdr   *nlh  = nlmsg_hdr(msg);
struct genlmsghdr *gnlh = nlmsg_data(nlh);
unsigned cmd = gnlh->cmd;
(void)arg;

if (!(g_filter_mask & (1u << cmd)))
return NL_OK;

switch (cmd) {
case MERLIN_NL_EV_PROG_LIFECYCLE:
return decode_prog_lifecycle(msg, g_json);
case MERLIN_NL_EV_MAP_UPDATE:
return decode_map_update(msg, g_json);
case MERLIN_NL_EV_TELEMETRY_SAMPLE:
return decode_telemetry(msg, g_json);
case MERLIN_NL_EV_NAMESPACE:
return decode_namespace(msg, g_json);
case MERLIN_NL_EV_ATTESTATION_REQ:
case MERLIN_NL_EV_ATTESTATION_RESP:
return decode_attestation(msg, g_json);
case MERLIN_NL_EV_VERIFIER_REJECT:
return decode_reject(msg, g_json);
case MERLIN_NL_EV_RATELIMIT_DROP:
return decode_ratelimit_drop(msg, g_json);
default:
fprintf(stderr, "merlin-watcher: unknown cmd %u\n", cmd);
return NL_OK;
}
}

int main(int argc, char **argv)
{
int family_id, group_id, rc;

static const struct option opts[] = {
{ "json",   no_argument,       0, 'j' },
{ "filter", required_argument, 0, 'f' },
{ "help",   no_argument,       0, 'h' },
{ 0, 0, 0, 0 },
};
int o;
while ((o = getopt_long(argc, argv, "jf:h", opts, NULL)) != -1) {
switch (o) {
case 'j': g_json = true; break;
case 'f': g_filter_mask = parse_filter(optarg); break;
case 'h':
default:
fprintf(stderr,
"usage: %s [--json] [--filter c1,c2,...]\n",
argv[0]);
return o == 'h' ? 0 : 2;
}
}

g_sock = nl_socket_alloc();
if (!g_sock) { perror("nl_socket_alloc"); return 1; }
nl_socket_disable_seq_check(g_sock);
nl_socket_set_buffer_size(g_sock, 8 * 1024 * 1024, 0);

if (genl_connect(g_sock) < 0) {
fprintf(stderr, "genl_connect failed: %s\n", strerror(errno));
return 1;
}

family_id = genl_ctrl_resolve(g_sock, MERLIN_NL_FAMILY_NAME);
if (family_id < 0) {
fprintf(stderr,
"genl_ctrl_resolve '%s' failed: %s\n"
"  (hint: insmod test/stub_genl.ko --or-- wait for\n"
"   the in-tree kernel/merlin/ module to land)\n",
MERLIN_NL_FAMILY_NAME, nl_geterror(family_id));
return 1;
}

group_id = genl_ctrl_resolve_grp(g_sock,
 MERLIN_NL_FAMILY_NAME,
 MERLIN_NL_MCGRP_EVENTS);
if (group_id < 0) {
fprintf(stderr, "resolve_grp '%s' failed: %s\n",
MERLIN_NL_MCGRP_EVENTS, nl_geterror(group_id));
return 1;
}

if (nl_socket_add_membership(g_sock, group_id) < 0) {
fprintf(stderr, "add_membership failed: %s\n", strerror(errno));
return 1;
}

if (!g_json)
fprintf(stderr,
"[merlin-watcher] subscribed to '%s/%s' "
"(family=%d, group=%d)\n",
MERLIN_NL_FAMILY_NAME, MERLIN_NL_MCGRP_EVENTS,
family_id, group_id);

nl_socket_modify_cb(g_sock, NL_CB_VALID, NL_CB_CUSTOM,
    on_event, NULL);

for (;;) {
rc = nl_recvmsgs_default(g_sock);
if (rc < 0 && rc != -NLE_NOMEM) {
fprintf(stderr, "nl_recvmsgs: %s\n", nl_geterror(rc));
break;
}
}

nl_socket_free(g_sock);
return 0;
}
