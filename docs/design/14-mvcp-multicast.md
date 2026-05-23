# 14 — MVCP Multicast Channel (`MERLIN_NL_CTRL`)

Status: design
Owner: PacketFive
Cross-reference: `09-mvcp-kernel-uapi.md` §3.6

> **Implementation status.** Design-complete; kernel
> implementation deferred until the in-tree merge of
> `kernel/merlin/` (the out-of-tree module cannot cleanly
> register a Generic Netlink family because Genl
> registration requires kernel symbol export and module
> reload-safe IDs).  A user-space subscriber prototype
> lives at `tools/merlin-watcher/`.

This document is the protocol specification for the MERLIN-V
event channel that hyperscale operators and fleet daemons
subscribe to.  It refines §3.6 of `09-mvcp-kernel-uapi.md`
with concrete message formats, subscription semantics, and
the producer-side authentication story.

---

## 1. Why a multicast channel

MERLIN-V is deployed at fleet scale.  Operators need to
observe, in near real time:

- **Program lifecycle events**: which programs loaded,
  attached, detached, unloaded.
- **Map state**: when an atomic map-batch transaction
  committed (so a downstream system can refresh its view).
- **Telemetry samples**: periodic stats per program (run
  count, run time, packet count, ...).
- **Attestation traffic**: someone asked for a quote on a
  loaded program; here is the response.
- **Namespace events**: a new MERLIN-V namespace was
  created / torn down.

The alternative --- per-event ioctl polling --- does not
scale.  A multicast channel where many subscribers can
listen to the same event stream with zero per-subscriber
kernel cost is the standard Linux solution; we use
Generic Netlink because it is the kernel's standard
extensible mechanism.

---

## 2. The Netlink family

```
Family name:     "merlin_nl"
Family version:  1
Multicast group: "events"
```

Registered via `genl_register_family` at module init.
Subscribers `bind()` an `AF_NETLINK` socket to family id 0
(generic) then `setsockopt(NETLINK_ADD_MEMBERSHIP)` for
the `events` group's group id (obtained via the
`CTRL_CMD_GETFAMILY` query).

The standard `libnl` user-space library handles the
boilerplate; `tools/merlin-watcher/` uses it.

### 2.1 Permissions

Any process that has `CAP_BPF` (or, on systems with a
separate `CAP_MERLIN` capability bit, `CAP_MERLIN`) can
subscribe.  Unprivileged subscription is intentionally
disabled: telemetry samples could leak information about
loaded programs in security-sensitive deployments.

A `MERLIN_NLATTR_FLAGS` bit
(`MERLIN_NL_F_PRIVILEGED_ONLY`) on the producer side
restricts certain event classes (notably attestation
responses) to subscribers that also carry
`CAP_SYS_ADMIN`.

---

## 3. Event types

Seven event commands, each with its own netlink attribute
schema.  All events are unicast-broadcast: the producer
fills in a `genlmsg` and `genlmsg_multicast`s it to the
`events` group.

| Command name (cmd id)              | Producer | Payload kind |
|------------------------------------|----------|--------------|
| `MERLIN_NL_EV_PROG_LIFECYCLE` (1)  | kernel/loader | program-lifecycle attrs |
| `MERLIN_NL_EV_MAP_UPDATE` (2)      | kernel/maps   | map-update attrs        |
| `MERLIN_NL_EV_TELEMETRY_SAMPLE`(3) | kernel/dispatch | telemetry attrs       |
| `MERLIN_NL_EV_NAMESPACE` (4)       | kernel/nsfs   | namespace attrs         |
| `MERLIN_NL_EV_ATTESTATION_REQ` (5) | user (daemon) | quote-request attrs     |
| `MERLIN_NL_EV_ATTESTATION_RESP`(6) | kernel        | quote-response attrs    |
| `MERLIN_NL_EV_VERIFIER_REJECT` (7) | kernel/loader | reject attrs            |

Below: the attribute schema for each.

### 3.1 `MERLIN_NL_EV_PROG_LIFECYCLE`

```
MERLIN_NLATTR_NS_ID      u32  required
MERLIN_NLATTR_PROG_ID    u32  required
MERLIN_NLATTR_ACTION     u8   required  enum { LOADED, ATTACHED, DETACHED, UNLOADED }
MERLIN_NLATTR_TAG        bytes(8) required  SHA-256 prefix
MERLIN_NLATTR_TIME_NS    u64  required
MERLIN_NLATTR_PROG_NAME  str  optional
```

Emitted on `MERLIN_PROG_LOAD`,
`MERLIN_LINK_CREATE/UPDATE/DETACH`,
and program free.  ~64 bytes per event.

### 3.2 `MERLIN_NL_EV_MAP_UPDATE`

```
MERLIN_NLATTR_NS_ID      u32  required
MERLIN_NLATTR_MAP_ID     u32  required
MERLIN_NLATTR_TXN_ID     u64  required  (0 if not from a batch txn)
MERLIN_NLATTR_OP         u8   required  enum { INSERT, UPDATE, DELETE, REPLACE_ALL }
MERLIN_NLATTR_TIME_NS    u64  required
MERLIN_NLATTR_KEY_HASH   bytes(8) optional   hash of the affected key
MERLIN_NLATTR_VALUE_HASH bytes(8) optional   hash of the new value
```

Hashed key/value (not the cleartext) so a passive observer
on the channel doesn't learn user data while still being
able to detect lost updates and reorder them.

### 3.3 `MERLIN_NL_EV_TELEMETRY_SAMPLE`

```
MERLIN_NLATTR_NS_ID         u32  required
MERLIN_NLATTR_PROG_ID       u32  required
MERLIN_NLATTR_TIME_NS       u64  required
MERLIN_NLATTR_RUN_CNT       u64  required
MERLIN_NLATTR_RUN_TIME_NS   u64  required
MERLIN_NLATTR_RECURSION_MISSES u64 required
MERLIN_NLATTR_PKT_DROP      u64  optional  (XDP-V / MVDP only)
MERLIN_NLATTR_PKT_PASS      u64  optional
```

Period configurable per program type (default: 10 s,
off by default).

### 3.4 `MERLIN_NL_EV_NAMESPACE`

```
MERLIN_NLATTR_NS_ID      u32  required
MERLIN_NLATTR_ACTION     u8   required  enum { CREATED, DESTROYED }
MERLIN_NLATTR_NSFS_PATH  str  optional
MERLIN_NLATTR_TIME_NS    u64  required
```

### 3.5 `MERLIN_NL_EV_ATTESTATION_REQ`

A producer (typically `merlind`) requests a fresh
attestation quote for a target program.  The kernel
responds with an `EV_ATTESTATION_RESP` carrying the quote.

```
MERLIN_NLATTR_NONCE      bytes(32) required  randomly chosen by requester
MERLIN_NLATTR_TARGET_PROG_ID u32   required
MERLIN_NLATTR_TIME_NS    u64       required
```

### 3.6 `MERLIN_NL_EV_ATTESTATION_RESP`

```
MERLIN_NLATTR_NONCE      bytes(32) required  echoed
MERLIN_NLATTR_TARGET_PROG_ID u32   required
MERLIN_NLATTR_QUOTE      bytes     required  the signed attestation quote
MERLIN_NLATTR_QUOTE_SIG_ALG u8     required
MERLIN_NLATTR_TIME_NS    u64       required
```

Restricted to `CAP_SYS_ADMIN` subscribers
(`MERLIN_NL_F_PRIVILEGED_ONLY`).

### 3.7 `MERLIN_NL_EV_VERIFIER_REJECT`

Emitted when a program is rejected.  Useful for fleet
operators investigating why a deployment failed.

```
MERLIN_NLATTR_NS_ID         u32  required
MERLIN_NLATTR_TAG           bytes(8) required
MERLIN_NLATTR_REJECT_REASON str  required  human-readable
MERLIN_NLATTR_REJECT_PC     u32  optional  first-rejected PC
MERLIN_NLATTR_TIME_NS       u64  required
```

---

## 4. Subscription semantics

### 4.1 Filter messages

A subscriber that only cares about, e.g., telemetry
samples can install a netlink message-filter
(`SO_ATTACH_FILTER`) on the socket to drop the other
event classes in-kernel.  Standard Linux netlink pattern.

### 4.2 Buffer overflow

If a subscriber's socket recv buffer overflows, the
kernel sets `NLMSG_OVERRUN` in the socket's error queue.
The subscriber is expected to recover by:

1. Reading the overrun marker.
2. Issuing an out-of-band `MERLIN_PROG_GET_NEXT_ID` /
   `MERLIN_MAP_GET_NEXT_ID` walk to rebuild state.
3. Resuming subscription.

This is the standard Netlink pattern.  Subscribers that
need lossless delivery should set a large socket buffer
(`SO_RCVBUFFORCE`) and prioritise reading.

### 4.3 Rate limiting

The kernel-side producer is rate-limited per event class:

| Class                | Default rate cap |
|----------------------|------------------|
| PROG_LIFECYCLE       | 1000 events / s  |
| MAP_UPDATE           | 10000 events / s |
| TELEMETRY_SAMPLE     | 100 events / s   |
| NAMESPACE            | 100 events / s   |
| ATTESTATION_*        | 100 events / s   |
| VERIFIER_REJECT      | 1000 events / s  |

Caps tunable per-event-class via sysctl.  When the cap is
exceeded, events are dropped at source (not buffered).
A `MERLIN_NL_EV_RATELIMIT_DROP` event (cmd 8) is emitted
periodically reporting how many events of each class were
dropped.

### 4.4 Authentication

Multicast netlink messages are unauthenticated by default;
the kernel is trusted to be the producer.  For deployments
that need stronger guarantees (e.g.\ defending against a
malicious user-space process spoofing as the producer ---
not possible in vanilla Linux because user processes
cannot send to a kernel-registered Genl family they did
not register, but mentioned here for completeness),
quotes carry a signature from the MERLIN-V keyring
(`docs/design/11-mvcp-attestation.md`).

---

## 5. User-space prototype

`tools/merlin-watcher/` is a libnl-based subscriber that:

- Joins the `merlin_nl/events` multicast group.
- Decodes every event.
- Prints a human-readable line.
- Optionally dumps a JSON-Lines record per event for fleet
  ingestion.

Build and run:

```bash
cd tools/merlin-watcher
make
sudo ./merlin-watcher --json
[merlin-watcher] subscribed to family 'merlin_nl', group 'events' (id=NN)
{"ts":1731234567,"ev":"prog_lifecycle","ns":0,"prog":1,"act":"loaded","tag":"01a2b3..."}
{"ts":1731234568,"ev":"telemetry","ns":0,"prog":1,"run_cnt":1024,...}
```

The tool currently runs against a stub `merlin_nl`
provided by an out-of-tree shim module
(`tools/merlin-watcher/test/stub_genl.c`); the
in-kernel producer lands when `kernel/merlin/` is
upstream.

---

## 6. Implementation tasks (post-upstream)

After `kernel/merlin/` merges, this design becomes the
following concrete implementation tasks:

1. `kernel/merlin/netlink.c`: register `merlin_nl` Genl
   family; expose `events` multicast group.
2. `kernel/merlin/netlink.c`: implement per-event emit
   helpers (`merlin_nl_emit_prog_lifecycle()`,
   `merlin_nl_emit_map_update()`, etc.).
3. `kernel/merlin/loader.c`: call
   `merlin_nl_emit_prog_lifecycle(LOADED)` at the end of
   `MERLIN_PROG_LOAD` success path.
4. Same for `LINK_CREATE/UPDATE/DETACH`, namespace
   create/destroy, attestation responses.
5. `kernel/merlin/maps.c`: hook the txn commit path to
   emit MAP_UPDATE events.
6. `kernel/merlin/dispatch.c`: periodic telemetry sample
   emitter (timer-driven, per program type).
7. Sysctl knobs for per-class rate caps.
8. selftest: a kernel-side test that loads a program,
   subscribes from user space, checks the lifecycle
   event arrives.

Estimated size: roughly 600 lines of kernel C across the
new `netlink.c` and the hook points in existing files.

---

## 7. Open questions

- **Wire format stability.**  Once shipped in a released
  kernel, the netlink attribute schema is UAPI-stable.
  We should pin all attribute IDs in
  `uapi/linux/merlin_nl.h` before the RFC posting.
- **Per-CPU emit.**  High-rate events on a multi-core
  system should emit per-CPU into ring buffers and have
  a single emitter thread aggregate to netlink.  Open
  whether this is needed in v1.
- **Field omission policy.**  Optional attributes default
  to ``not present'' on emit and ``zero'' on subscribe.
  Subscribers should always check `attrs[X] != NULL`
  before dereferencing.  This is the standard Netlink
  pattern.
- **Backpressure to producer.**  No mechanism today.  If
  every subscriber is slow we drop events; a future
  enhancement could pause emission per-class until
  subscriber catches up.

---

## 8. Closes

This document closes the `proto-mvcp-multicast` todo in
the design sense.  The kernel implementation tasks above
move into the in-tree `kernel/merlin/` work that follows
the RFC merge.

## Assisted-by

Copilot-CLI:Claude-Opus
