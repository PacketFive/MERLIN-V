# 08 — MVDP and AF\_MVDP

*Status: draft. Profiles, helper ABI, ELF format, and verifier strategy
pinned in earlier docs; MVDP and AF\_MVDP build on those.*

## 1. Scope and motivation

MERLIN-V needs a programmable network data path. The existing kernel
data paths — XDP and AF\_XDP — solve a problem we want to solve too,
but they are bound to eBPF programs at the verifier, helper, and
context layers, **and** their userspace surface forces a four-step
dance (load program, create link, populate `XSKMAP`, bind socket) on
even the simplest single-consumer case. Four options were considered:

1. **Reuse XDP/AF\_XDP as-is.** Express MERLIN-V network programs
   through the `MERLIN_PROG_TYPE_XDP_V` cohabitation type (see
   [03-kernel-interfaces.md](03-kernel-interfaces.md) §2): the program
   sees a `xdp_md`-shaped context, returns `XDP_*` verdicts, and the
   kernel redirects into AF\_XDP sockets via `XSKMAP`.
2. **Replace XDP/AF\_XDP for MERLIN-V.** Forbid `XDP_V` cohabitation
   and require all MERLIN-V network programs to use a new data path.
3. **Add MVDP and AF\_MVDP as a parallel data path, copying the
   XDP/AF\_XDP control-surface split.** Keep `XDP_V` for porting;
   add MVDP as the native MERLIN-V data path with its own context,
   verdicts, helpers, attach modes, maps, and socket family — but
   replicate XDP's "program is a separate object from the socket"
   model.
4. **Option 3, but with a unified control surface.** Same parallel
   data path, but the *user-visible* API has the program attached
   directly to an `AF_MVDP` socket via a sockopt, with `bind()`
   installing it on the queue. The `MERLIN_LINK_CREATE` path is
   retained as the alternative for headless (no userspace consumer)
   programs and for advanced fan-out topologies. `MVSKMAP` becomes
   opt-in for multi-consumer fan-out, not the entry point.

This document chooses **option 4**.

The rationale is the same one that motivated MERLIN-V over eBPF in
the first place: the existing data path is *correct* but encodes
choices that pre-date the bytecode being a hardware ISA — and the
existing user-visible API encodes choices that pre-date 10+ years
of mailing-list feedback on how AF\_XDP is painful to set up. MVDP
is the answer to *"if AF\_XDP were designed now, with RISC-V bytecode,
the helper ABI from [02-isa-and-bytecode.md](02-isa-and-bytecode.md) §6,
and a decade of AF\_XDP UX hindsight already in hand, what would it
look like?"*

Concretely, MVDP gives us:

- A context whose field offsets and types are stable MERLIN-V UAPI,
  not borrowed from the in-kernel `struct xdp_buff` layout.
- Helpers reachable through the canonical `ecall + a7` ABI rather
  than the eBPF `BPF_CALL` opcode (which MERLIN-V does not have).
- A hook-mode story that includes hardware offload from day one
  as a *declared* (if not yet implemented) mode, because MERLIN-V's
  bytecode is the same RISC-V the offload engine executes — there
  is no language gap to translate across.
- An address family (`AF_MVDP`) whose data plane is bit-compatible
  with AF\_XDP (so existing zero-copy userspace ports cheaply) but
  whose **control plane is unified**: program-via-sockopt + bind,
  no separate "link" step for the common case.

The non-goals are equally important:

- **Not a replacement for XDP/AF\_XDP.** The kernel keeps both.
  Existing eBPF programs continue to use XDP. The cohabitation
  program type `MERLIN_PROG_TYPE_XDP_V` remains supported for
  porting MERLIN-V programs onto unmodified XDP hooks.
- **Not a new packet-classification language.** MVDP is a hook
  surface and a socket family, not a P4-style pipeline definition.
- **Not a kernel-bypass framework.** Programs run in the kernel
  (or on an offload target) under the verifier and helper
  contracts, and the userspace path goes through `bind()` and
  shared rings — the same trust model as AF\_XDP.

## 2. MVDP program type

### 2.1 Identifier

```c
#define MERLIN_PROG_TYPE_MVDP       7
```

Slotted after the cohabitation types in [03-kernel-interfaces.md](03-kernel-interfaces.md) §2.

### 2.2 Context: `struct mvdp_md`

The context delivered to an MVDP program. Field offsets and types
are MERLIN-V UAPI and are stable across kernel versions; the
verifier validates load/store against this layout. The struct is
not declared in C source the program compiles against — it is a
contract enforced at verification time, addressable via fixed
offsets from `a0` on program entry (see
[02-isa-and-bytecode.md](02-isa-and-bytecode.md) §6.1).

```c
struct mvdp_md {
    __u64 data;             /* pointer to start of frame                     */
    __u64 data_end;         /* pointer to one past end of frame              */
    __u64 data_meta;        /* pointer to start of user metadata area;
                               data_meta <= data; bytes in [data_meta, data)
                               are mutable by the program.                   */
    __u32 ingress_ifindex;  /* RX netdev ifindex                             */
    __u32 rx_queue_index;   /* RX queue on the netdev                        */
    __u32 egress_ifindex;   /* set by MVDP_REDIRECT; 0 otherwise             */
    __u32 frame_flags;      /* MVDP_FRAME_F_*                                */
    __u64 hw_timestamp;     /* NIC RX timestamp ns; 0 if unavailable         */
    __u32 csum_status;      /* MVDP_CSUM_*                                   */
    __u32 vlan_tci;         /* VLAN tag if stripped by HW; 0 otherwise       */
    __u32 hash;             /* RX hash; 0 if unavailable                     */
    __u32 _reserved;
};
```

`data`, `data_end`, and `data_meta` are typed pointers in the verifier's
type system; arithmetic on them is permitted only through the
bounded-pointer rules in [06-verifier.md](06-verifier.md) §3. Loads
of the integer fields are typed `scalar_t` with the natural width.

Frame flags:

```c
#define MVDP_FRAME_F_VLAN_PRESENT     (1u << 0)
#define MVDP_FRAME_F_HASH_VALID       (1u << 1)
#define MVDP_FRAME_F_TIMESTAMP_VALID  (1u << 2)
/* bits 3..31 reserved, must read as zero */
```

Checksum status:

```c
#define MVDP_CSUM_NONE        0  /* hardware did not check L3/L4 sum */
#define MVDP_CSUM_UNNECESSARY 1  /* hardware verified, OK            */
#define MVDP_CSUM_COMPLETE    2  /* hardware provided a partial sum  */
#define MVDP_CSUM_BAD         3  /* hardware checked, failed         */
```

### 2.3 Verdicts

```c
enum mvdp_action {
    MVDP_ABORTED  = 0,  /* program faulted or returned out-of-range; treated
                           as DROP and traced via tracepoint mvdp:mvdp_exception */
    MVDP_DROP     = 1,
    MVDP_PASS     = 2,  /* deliver to normal kernel netif_receive path        */
    MVDP_TX       = 3,  /* transmit out the ingress interface                 */
    MVDP_REDIRECT = 4,  /* deliver per per-CPU mvdp_redirect target           */
    MVDP_DELIVER  = 5,  /* deliver to the AF_MVDP socket that installed this
                           program (the unified-socket common case; §3)       */
};
```

Verdicts >= 6 are reserved and treated as `MVDP_ABORTED` for forward
compatibility. The verifier rejects programs that can return a
non-constant verdict outside the declared range.

The split between `MVDP_REDIRECT` (frame goes to a target chosen by
helper call into a map) and `MVDP_DELIVER` (frame goes to the socket
that owns the program) is what makes the unified socket model work:
the common single-consumer case uses `MVDP_DELIVER` and needs no
map; multi-consumer fan-out uses `MVDP_REDIRECT` with `MVSKMAP`.

### 2.4 Installation modes

A program is installed on a `(ifindex, queue_id)` in one of three
modes — `SKB`, `DRV`, or `HW` — which control *where* the program
runs. The mode is selected at install time via flags; the install
itself happens either via `bind()` on an `AF_MVDP` socket (the
common path, see §3) or via `MERLIN_LINK_CREATE` (the headless path,
see §4).

| Flag | Meaning | RFC v1? |
| ---- | ------- | ------- |
| `MVDP_FLAGS_SKB_MODE` | Generic hook via the receive softirq, after `napi_gro_receive` but before `netif_receive_skb_core`. Works on every netdev. Slower than driver-native — incurs the cost of skb allocation. | Yes |
| `MVDP_FLAGS_DRV_MODE` | Driver-native hook. The driver calls `merlin_mvdp_run(prog, frame)` from its RX path before constructing skbs. Requires driver opt-in. | Documented; not shipped |
| `MVDP_FLAGS_HW_MODE` | Program is loaded onto a NIC firmware or a MERLIN-V hardware accelerator that natively executes the verified RISC-V image. The kernel keeps only the link state and helper trampolines for control-plane operations. | Documented; not shipped |

Exactly one mode bit must be set. RFC v1 implements `SKB_MODE` only.
`DRV_MODE` is the natural follow-up; `HW_MODE` is the forward
reference to future MERLIN-V hardware acceleration. See
[07-jit-and-offload.md](07-jit-and-offload.md) §HW Offload for the
hardware side.

### 2.5 Coexistence with XDP

On any given `(ifindex, rx_queue_index)`:

| RFC v1 | Phase 2 |
| ------ | ------- |
| At most one of {XDP program, MVDP program, MERLIN_PROG_TYPE_XDP_V program} is active. Attempts to install a second return `-EBUSY`. | A multiplexer that runs a chain of programs in declared order, with verdict combination identical to multi-prog eBPF semantics. |

The per-netdev "active programmable data path" state is exposed
through `ethtool -n <dev>` (read-only) and via the `merlin link`
CLI. Whichever data path is active determines which userspace
socket families (AF\_XDP or AF\_MVDP) may bind to that queue.

### 2.6 Helpers

MVDP helpers are reachable through the canonical helper ABI
([02-isa-and-bytecode.md](02-isa-and-bytecode.md) §6.2): source-level
`li a7, ID; ecall`, loader-rewritten to a direct call into the
per-program-type helper trampoline. Helper IDs live in
`<merlin/helpers.h>` ([04-toolchain.md](04-toolchain.md)).

Initial helper set:

| Helper | Purpose |
| ------ | ------- |
| `mvdp_redirect(ifindex, flags)` | Set per-CPU redirect target to a netdev; verdict must be `MVDP_REDIRECT`. |
| `mvdp_redirect_map(map_fd, key, flags)` | Set per-CPU redirect target via a `DEVMAP`, `CPUMAP`, or `MVSKMAP`. |
| `mvdp_adjust_head(delta)` | Grow (negative) or shrink (positive) the headroom; bounded by reserved headroom in the frame. |
| `mvdp_adjust_tail(delta)` | Grow or shrink the tail; bounded by reserved tailroom. |
| `mvdp_adjust_meta(delta)` | Grow or shrink the metadata area; verifier re-types `data_meta` after the call. |
| `mvdp_load_bytes(off, dst_ptr, len)` | Bounded copy out of the frame to a verifier-typed buffer. |
| `mvdp_store_bytes(off, src_ptr, len)` | Bounded copy into the frame. |
| `mvdp_fib_lookup(params_ptr, len, flags)` | Route lookup in a netns; populates a caller-supplied result struct. |
| `mvdp_get_time_ns()` | Monotonic ns clock (CLOCK_MONOTONIC equivalent). |

Each helper has a fixed `a7` ID and a fixed signature documented in
`<merlin/helpers.h>`. The verifier checks signatures at call sites.

This list is **deliberately small for RFC v1.** Helpers are added
incrementally as MVDP gains workload coverage; each addition is a
UAPI promise.

## 3. AF\_MVDP socket family (the unified control plane)

### 3.0 The unified socket model

`AF_MVDP` is the **primary entry point** for installing an MVDP
program. The four-step AF\_XDP dance — load program, create link,
populate `XSKMAP`, bind socket — collapses to:

```c
int fd = socket(AF_MVDP, SOCK_RAW, 0);

/* Register the frame pool. */
setsockopt(fd, SOL_MVDP, MVDP_UMEM_REG,           &umem,    sizeof umem);
setsockopt(fd, SOL_MVDP, MVDP_RX_RING,            &n,       sizeof n);
setsockopt(fd, SOL_MVDP, MVDP_UMEM_FILL_RING,     &n,       sizeof n);
/* ...TX, COMPLETION rings if needed... */

/* Attach the verified MERLIN-V program (returned by MERLIN_PROG_LOAD). */
struct mvdp_prog_attach pa = {
    .prog_fd       = prog_fd,
    .install_flags = MVDP_FLAGS_SKB_MODE,
    .deliver_mode  = MVDP_DELIVER_DEFAULT,   /* see §3.8.2 */
    .replace       = false,
};
setsockopt(fd, SOL_MVDP, MVDP_PROG_ATTACH, &pa, sizeof pa);

/* Bind: this installs the program on the queue AND registers this
 * socket as the userspace consumer for "deliver" verdicts. */
bind(fd, (struct sockaddr *)&addr, sizeof addr);
```

Closing `fd` detaches the program and frees the queue binding. No
bpffs pin is required for the common single-consumer case. The
program lifetime is the socket fd lifetime.

When the program returns `MVDP_DELIVER` (a new verdict — see §2.3.1
below) the frame is placed on **this socket's** RX ring. When it
returns `MVDP_REDIRECT` the frame is placed where the verdict
target says (another netdev via DEVMAP, another CPU via CPUMAP, or
another AF\_MVDP socket via MVSKMAP). All other verdicts (DROP,
PASS, TX, ABORTED) behave exactly as in §2.3.

This single-socket-one-program-one-queue path is the design
target for ~95% of deployments. Headless and fan-out paths
(§4 below) handle the rest without making the common case complex.

### 3.1 Address family

```c
#define AF_MVDP    /* TBD; coordinate with upstream linux/socket.h */
#define PF_MVDP    AF_MVDP
```

The actual number is an upstream coordination ask. Until allocated,
out-of-tree builds use `AF_MAX + 1` and the in-tree patch series
requests the number from the networking maintainers. Userspace
should obtain it via `getaddrinfo`-equivalent introspection
(`/proc/net/protocols`) rather than hard-coding.

### 3.2 Socket creation

```c
int fd = socket(AF_MVDP, SOCK_RAW, 0);
```

Only `SOCK_RAW` is defined. `protocol` must be 0; non-zero is
reserved for future use. `CAP_NET_RAW` is required (same as AF\_XDP).
`CAP_MERLIN` is *additionally* required only if the program attached
via `MVDP_PROG_ATTACH` is not already a referenced fd held by this
process — that is, if the same process loaded the program it can
attach it without `CAP_MERLIN`; if the program fd was passed in via
SCM\_RIGHTS or merlinfs from a privileged installer, the consumer
need not be privileged.

### 3.3 UMEM registration

A "MERLIN-V UMEM" (`mumem` informally) is a userspace shared memory
area that holds frame data. The kernel pins it and uses it as a
DMA target. Registration is via setsockopt:

```c
struct mvdp_umem_reg {
    __u64 addr;              /* userspace virtual address of the area      */
    __u64 len;               /* length in bytes                            */
    __u32 chunk_size;        /* per-frame chunk size                       */
    __u32 headroom;          /* reserved bytes at start of each chunk      */
    __u32 flags;             /* MVDP_UMEM_F_*                              */
    __u32 tx_metadata_len;   /* per-frame TX metadata trailer, 0 if unused */
};

#define SOL_MVDP            /* TBD; coordinated with AF_MVDP allocation */
#define MVDP_UMEM_REG       1
```

Flags:

```c
#define MVDP_UMEM_F_UNALIGNED_CHUNK_FLAG  (1u << 0)
/* bits 1..31 reserved */
```

### 3.4 Rings

Four rings, in the AF\_XDP model:

| Ring | Direction | Producer | Consumer |
| ---- | --------- | -------- | -------- |
| `FILL` | Frames available to receive into | Userspace | Kernel |
| `RX` | Received frames waiting for userspace | Kernel | Userspace |
| `TX` | Frames userspace wants transmitted | Userspace | Kernel |
| `COMPLETION` | Frames whose transmit is done | Kernel | Userspace |

Ring sizes (in entries) are configured per-direction:

```c
#define MVDP_RX_RING            2
#define MVDP_TX_RING            3
#define MVDP_UMEM_FILL_RING     4
#define MVDP_UMEM_COMPLETION_RING 5
```

Each `setsockopt` takes a `__u32` count which must be a power of
two between 64 and 16384 inclusive.

### 3.5 Ring entry format

For RFC v1 the per-frame descriptor is **bit-compatible** with
AF\_XDP's:

```c
struct mvdp_desc {
    __u64 addr;     /* offset into UMEM (or unaligned address if flagged) */
    __u32 len;      /* frame length in bytes                              */
    __u32 options;  /* MVDP_DESC_OPT_*                                    */
};
```

This matches `struct xdp_desc` exactly. Rationale: existing
zero-copy userspace libraries (`libxdp`'s `xsk_*` family) can be
ported to AF\_MVDP by retargeting their socket creation, bind,
setsockopt names, and ring file descriptors — not by rewriting the
hot path. The differences live entirely in the control plane.

Descriptor options:

```c
#define MVDP_DESC_OPT_RX_HASH_VALID  (1u << 0)
#define MVDP_DESC_OPT_RX_TS_VALID    (1u << 1)
/* bits 2..31 reserved */
```

### 3.6 Bind

```c
struct sockaddr_mvdp {
    __kernel_sa_family_t sa_family;  /* AF_MVDP                         */
    __u16                _pad;
    __u32                ifindex;
    __u32                queue_id;
    __u32                flags;       /* MVDP_BIND_F_*                   */
    __u32                shared_umem_fd; /* 0 if not sharing             */
    __u32                _reserved;
};

#define MVDP_BIND_F_SHARED_UMEM   (1u << 0)
#define MVDP_BIND_F_ZEROCOPY      (1u << 1)
#define MVDP_BIND_F_NEEDS_WAKEUP  (1u << 2)
/* bits 3..31 reserved */
```

`bind()` semantics in the unified model:

- The socket must already have a program attached via
  `MVDP_PROG_ATTACH` (§3.8.1), **unless** the program is being
  installed in fan-out mode and another AF\_MVDP socket already
  has installed the program on this `(ifindex, queue_id)` — in
  which case this socket binds as an additional consumer. The
  fan-out shared-program case is documented in §4.2.
- If the socket has a program attached, `bind()`:
  1. installs the program on `(ifindex, queue_id)` per the
     `install_flags` from the most recent `MVDP_PROG_ATTACH` (SKB /
     DRV / HW mode), or fails with `-EBUSY` if another data path
     (XDP, XDP\_V, or another MVDP installation) already owns the
     queue;
  2. registers this socket as the userspace consumer for
     `MVDP_DELIVER` verdicts; and
  3. wires up the rings configured in §3.3–§3.4.
- If an AF\_XDP socket is already bound to `(ifindex, queue_id)`
  the call returns `-EBUSY`. Cross-family coexistence on the same
  queue is a Phase-2 multiplexer feature.
- `MVDP_BIND_F_ZEROCOPY` requires `DRV_MODE`. Without it the kernel
  falls back to copy mode and the bind succeeds.

`close(fd)` does the reverse: it detaches the program (if this is
the last AF\_MVDP socket holding it), tears down the rings, and
frees the queue binding.

### 3.7 Wakeup and poll

`poll(2)` on the socket fd works the same as for AF\_XDP:

- `POLLIN` indicates RX entries pending in the RX ring.
- `POLLOUT` indicates COMPLETION entries pending in the COMPLETION
  ring (i.e. there is room in the TX ring for new descriptors).

When `MVDP_BIND_F_NEEDS_WAKEUP` is set, after producing into FILL
or TX the userspace must issue `sendto(fd, NULL, 0, MSG_DONTWAIT, NULL, 0)`
to ask the kernel to start processing — same protocol as AF\_XDP.

### 3.8 Sockopts

#### 3.8.1 Program attach / detach (control)

```c
struct mvdp_prog_attach {
    __u32 prog_fd;            /* fd from MERLIN_PROG_LOAD                 */
    __u32 install_flags;      /* MVDP_FLAGS_SKB_MODE | DRV_MODE | HW_MODE */
    __u32 deliver_mode;       /* enum mvdp_deliver_mode                   */
    __u32 replace_revision;   /* 0 = no replace; else CAS guard           */
};

enum mvdp_deliver_mode {
    MVDP_DELIVER_DEFAULT  = 0,  /* deliver verdict -> this socket's RX     */
    MVDP_DELIVER_VIA_MAP  = 1,  /* program must use MVSKMAP for fan-out;
                                   this socket is just a control handle   */
    MVDP_DELIVER_NONE     = 2,  /* headless install; reject MVDP_DELIVER
                                   verdicts (program is for DROP/TX/REDIR) */
};

#define MVDP_PROG_ATTACH    20
#define MVDP_PROG_DETACH    21
#define MVDP_PROG_QUERY     22   /* getsockopt: read back attach state    */
```

`MVDP_PROG_ATTACH` records the program against the socket but does
**not** install it on the netdev queue until `bind()` runs. This
allows full configuration of rings and UMEM before the program
goes live.

`MVDP_PROG_QUERY` (getsockopt) returns a `struct mvdp_prog_attach`
describing the currently attached program plus its revision; an
external controller can use this for safe atomic replace via
`replace_revision`.

The `replace_revision` field enables atomic in-place program
replacement: a fresh `MVDP_PROG_ATTACH` with `replace_revision`
equal to the current revision swaps the program without dropping
frames. Mismatched revision returns `-EAGAIN`; this is the same
optimistic-concurrency contract MVCP uses for fleet-managed
deployments (see [09-mvcp-kernel-uapi.md](09-mvcp-kernel-uapi.md)).

`MVDP_PROG_DETACH` removes the program. If the socket is still
bound, the queue's data path goes inactive (frames pass to the
stack unmodified). Closing the socket implicitly detaches.

#### 3.8.2 Introspection

```c
#define MVDP_STATISTICS    16   /* getsockopt: struct mvdp_statistics    */
#define MVDP_OPTIONS       17   /* getsockopt: __u32 flag word           */
#define MVDP_RING_OFFSETS  18   /* getsockopt: struct mvdp_ring_offsets  */
```

`struct mvdp_statistics` returns RX dropped, RX invalid descriptors,
TX invalid descriptors, RX ring full, COMPLETION ring full, and a
versioned trailer for forward compatibility. Layout TBD with the
RFC; mirrors the AF\_XDP statistics struct.

## 4. Alternative install paths

The unified socket model in §3 is the *common* entry. Two
alternatives exist for cases the common path does not cover.

### 4.1 Headless: `MERLIN_LINK_CREATE`

A program may need to run on a netdev queue with **no** userspace
consumer — for example, a pure in-kernel routing or DDoS-filter
program that only ever returns DROP / PASS / TX / REDIRECT (to
another netdev or CPU), never DELIVER. In this case there is no
`AF_MVDP` socket to "own" the program.

For headless installs the program is installed via
`MERLIN_LINK_CREATE` with `attach_type = MERLIN_ATTACH_MVDP`:

```c
union merlin_attr attr = {
    .link_create = {
        .prog_fd        = prog_fd,
        .target_ifindex = ifindex,
        .target_queue   = queue_id,
        .attach_type    = MERLIN_ATTACH_MVDP,
        .flags          = MVDP_FLAGS_SKB_MODE | MVDP_INSTALL_F_HEADLESS,
    }
};
int link_fd = merlin(MERLIN_LINK_CREATE, &attr, sizeof attr);
```

`MVDP_INSTALL_F_HEADLESS` declares the install is consumer-free.
The verifier rejects programs that can return `MVDP_DELIVER` under
this flag. The link\_fd is the lifetime handle; closing it detaches
the program. The link\_fd may be pinned in `merlinfs` for processes
that need to outlive their controller.

This path is also what a daemon-managed install uses: the daemon
(e.g. `merlind` — see [10-mvcp-daemon-and-fleet.md](10-mvcp-daemon-and-fleet.md))
holds the link\_fd and gates userspace consumers separately.

### 4.2 Fan-out: AF\_MVDP + `MVSKMAP`

A program serving multiple userspace consumers on the *same*
queue uses the unified socket model with `deliver_mode =
MVDP_DELIVER_VIA_MAP`. The flow:

1. First consumer opens an `AF_MVDP` socket, attaches the program
   with `deliver_mode = MVDP_DELIVER_VIA_MAP`, binds. The program
   is installed on the queue; this socket is registered.
2. Second and subsequent consumers open `AF_MVDP` sockets,
   **do not** attach a program (`MVDP_PROG_ATTACH` is omitted),
   and bind to the same `(ifindex, queue_id)`. The kernel accepts
   the bind because a program already owns the queue; the socket
   is added to the consumer set.
3. Userspace populates a `MVSKMAP` with the socket fds.
4. The program uses `mvdp_redirect_map(mvskmap, key, flags)` and
   returns `MVDP_REDIRECT`; the chosen consumer's RX ring receives
   the frame.

`MVDP_DELIVER` is rejected by the verifier when `deliver_mode ==
MVDP_DELIVER_VIA_MAP`: the program must use the explicit map
redirect, never the implicit "this socket" delivery, because in
fan-out mode the *concept* of "this socket" is ambiguous.

This path requires `MVSKMAP` and is the only case where the user
ever interacts with `MVSKMAP` directly. The single-consumer common
case (§3) uses no maps at all.

## 5. Map type: `MERLIN_MAP_TYPE_MVSKMAP`

```c
#define MERLIN_MAP_TYPE_MVSKMAP   10  /* AF_MVDP redirect */
/* 11..127  reserved for future MERLIN-V map types     */
/* 128..255 reserved for vendor / offload-specific      */
```

Appended to the map type list in
[02-isa-and-bytecode.md](02-isa-and-bytecode.md) §8.5. A `MVSKMAP`
holds AF\_MVDP socket file descriptors (or rather their kernel
references); an MVDP program redirects into a slot by calling
`mvdp_redirect_map(mvskmap_fd, key, flags)` with verdict
`MVDP_REDIRECT`.

Validation rules:

- A socket fd inserted into a `MVSKMAP` must already be bound.
- The `(ifindex, queue_id)` of every socket in a single
  `MVSKMAP` should match the netdev the redirecting program is
  attached to. The kernel does not enforce this at map insert
  time but rejects mismatched redirects at run time and bumps
  the `MVDP_STATISTICS` invalid-descriptor counter.

`DEVMAP` and `CPUMAP` (existing types 8 and TBD) work with MVDP
unchanged; semantics mirror their XDP equivalents.

## 6. Hardware offload outlook

`MVDP_FLAGS_HW_MODE` is reserved because MERLIN-V's instruction
set is RISC-V — the same ISA executed by a hardware accelerator
implementing a verified MERLIN-V image. A future MERLIN-V
hardware acceleration track is expected to map cleanly onto MVDP:

- The verified bytecode image is uploaded to the accelerator (no
  re-translation).
- Helpers reachable via `ecall + a7` are implemented by accelerator
  firmware exposing the same `a7` numbers; the loader rewrite step
  on the host is replaced by an equivalent rewrite step on the
  accelerator.
- The accelerator delivers frame verdicts back through a hardware
  ring; the kernel completes the verdict (PASS / DROP / TX /
  REDIRECT) using its existing per-CPU redirect machinery.

This is documented as the architectural reason MVDP is worth
defining separately rather than borrowing XDP. No hardware is
shipped under RFC v1; the design only commits to leaving the
flag space and helper-call protocol HW-friendly.

A related research-adjacent project — KESTREL-V at the University
of Limerick — pursues hardware acceleration of eBPF; MERLIN-V's
hardware story is its own and progresses independently, with
MVDP as its natural data-path target.

## 7. Coexistence summary

| State on `(ifindex, queue_id)` | Allowed userspace bind |
| ------------------------------ | ---------------------- |
| No programmable data path attached | None |
| XDP attached (eBPF program) | AF\_XDP only |
| MVDP attached (MERLIN-V program) | AF\_MVDP only |
| `MERLIN_PROG_TYPE_XDP_V` attached (MERLIN-V program on XDP hook) | AF\_XDP only — the hook is XDP, MERLIN-V's role is bytecode |

Cross-queue mixing on the same netdev is unrestricted: queue 0 may
have XDP + AF\_XDP, queue 1 may have MVDP + AF\_MVDP.

## 8. UAPI surface impact

The MVDP and AF\_MVDP design adds the following to the UAPI surface
(codified in [`uapi/linux/merlin.h`](uapi/linux/merlin.h) and
[`uapi/linux/if_mvdp.h`](uapi/linux/if_mvdp.h)):

- New program type: `MERLIN_PROG_TYPE_MVDP`.
- New install-mode flag space: `MVDP_FLAGS_{SKB,DRV,HW}_MODE`,
  `MVDP_INSTALL_F_HEADLESS`.
- New attach type: `MERLIN_ATTACH_MVDP` (used only on the headless
  path; the unified-socket path installs implicitly through
  `bind()`).
- New map type: `MERLIN_MAP_TYPE_MVSKMAP`.
- New address family: `AF_MVDP` (upstream allocation request).
- New sockopt level: `SOL_MVDP`.
- New socket option numbers under `SOL_MVDP`:
  control: `MVDP_PROG_ATTACH`, `MVDP_PROG_DETACH`, `MVDP_PROG_QUERY`;
  data plane: `MVDP_UMEM_REG`, `MVDP_UMEM_FILL_RING`,
  `MVDP_UMEM_COMPLETION_RING`, `MVDP_RX_RING`, `MVDP_TX_RING`;
  introspection: `MVDP_STATISTICS`, `MVDP_OPTIONS`,
  `MVDP_RING_OFFSETS`, `MVDP_MMAP_OFFSETS`.
- New verdict enum: `enum mvdp_action` including `MVDP_DELIVER`.
- New deliver-mode enum: `enum mvdp_deliver_mode`.
- New control struct: `struct mvdp_prog_attach`.
- New context struct (verifier-validated, not a C-source struct):
  `struct mvdp_md`.
- New helper IDs: `mvdp_redirect`, `mvdp_redirect_map`,
  `mvdp_adjust_head`, `mvdp_adjust_tail`, `mvdp_adjust_meta`,
  `mvdp_load_bytes`, `mvdp_store_bytes`, `mvdp_fib_lookup`,
  `mvdp_get_time_ns` (`a7` numbers assigned by
  [04-toolchain.md](04-toolchain.md) §helpers).
- New tracepoint: `mvdp:mvdp_exception`.

Concurrency contract: `MVDP_PROG_ATTACH` with `replace_revision`
matching the current attached-program revision is the atomic
in-place replace operation used by fleet controllers (see
[09-mvcp-kernel-uapi.md](09-mvcp-kernel-uapi.md)).

## 9. What this document does *not* commit to

Open items deferred to follow-ups:

- **Multi-prog cohabitation.** The Phase-2 multiplexer that lets
  XDP and MVDP coexist on the same `(ifindex, queue_id)` is
  out of scope here. The architectural shape (chain order, verdict
  combination) is sketched in [03-kernel-interfaces.md](03-kernel-interfaces.md)
  §4 and inherits whatever upstream decides for multi-prog XDP.
- **Driver-native MVDP API.** Whether drivers expose MVDP through
  a new `ndo_mvdp` callback, an extension of `ndo_xdp`, or a
  generic "programmable data path" abstraction. RFC v1 ships
  `SKB_MODE` only; the driver API is a Phase-2 ask informed by
  upstream feedback.
- **Hardware offload bytecode upload protocol.** The accelerator
  upload format will be specified in
  [07-jit-and-offload.md](07-jit-and-offload.md) when the first
  hardware target lands. Today it is forward-referenced only.
- **Wire-compatible XDP hint exchange.** XDP recently grew a
  hints API for hardware metadata; MVDP's `struct mvdp_md`
  inlines the equivalent fields, but the question of whether to
  also accept the XDP hints kfunc shape (for source-code
  portability) is open.
- **Per-program statistics.** `MVDP_STATISTICS` returns
  per-socket counters today; per-program counters (RX'd, dropped,
  redirected, aborted) are a Phase-2 addition.
