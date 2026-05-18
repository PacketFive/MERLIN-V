# `merlin-ns` — prototype MVCP namespaces

The seventh user-space code prototype of MERLIN-V.  Implements
**MVCP program namespaces** per
[`../../docs/design/09-mvcp-kernel-uapi.md`](../../docs/design/09-mvcp-kernel-uapi.md) §3.4
— scoping rules and inheritance semantics for MERLIN-V namespaces.

## What the prototype proves

- The canonical config struct `merlin_ns_config_v1` is pinned
  in [`../../docs/design/uapi/merlin/namespace.h`](../../docs/design/uapi/merlin/namespace.h).
- The **subset rule** (child can only narrow parent, never widen)
  holds across all four dimensions: attach types, helpers,
  kfuncs, quotas.
- The **scope-check decision** at program-load time produces
  the right accept / reject verdict in every test case, with
  the right offending-id surfaced for diagnostics.
- The **quota model** (treating 0 as unlimited; inherit-by-zero
  on compose; arithmetic on usage at check time) is unambiguous.

## Subcommands

```
merlin-ns create  --name <s> [--from <parent.ns>]
                  [--attach LIST] [--helpers LIST] [--kfuncs LIST]
                  [--max-progs N] [--max-maps N]
                  [--max-map-mem BYTES] [--max-prog-mem BYTES]
                  [--inherit] [--seal]  -o <out.ns>

merlin-ns dump    <file.ns>

merlin-ns check   --ns <file.ns> --attach T --helpers LIST
                  [--kfuncs LIST] [--usage progs:N,maps:M,...]
                  [--map-mem N] [--prog-mem N]

merlin-ns compose --child <c.ns> --parent <p.ns> -o <eff.ns>
```

LIST is comma-separated decimal-or-hex IDs (`0x201,0x110,1`).
A namespace is a single binary file holding
`struct merlin_ns_config_v1` (a fixed-size, version-prefixed
record); the eventual kernel implementation will hold the same
struct in kernel memory referenced by an fd.

## Build & test

```bash
make
make test    # 12-case battery
```

## Test battery (12/12 pass)

```
[PASS] create parent
[PASS] dump reports counts (attach=1, helper=3)
[PASS] check accepts permitted program
[PASS] check rejects disallowed helper (0x303)
[PASS] check rejects wrong attach (type 5)
[PASS] check rejects when at prog quota
[PASS] check rejects over map_mem quota
[PASS] compose accepts child subset
[PASS] compose rejects child widening attach
[PASS] compose rejects child widening helper
[PASS] compose rejects child widening max_progs
[PASS] effective ns drops parent's helper not in child
```

## The inheritance rule

```
parent = { attach: {MVDP}, helpers: {ktime, mvdp_redirect, map_lookup},
           max_progs: 10, max_maps: 10, max_map_mem: 1 MiB }

OK child:  { attach: {MVDP}, helpers: {ktime}, max_progs: 5 }
   -> compose produces an effective ns with the SUBSET of perms
      and inherits parent's finite quotas where child set 0.

REJECT child: child adds attach type {KPROBE} that parent did not have.
REJECT child: child references helper {0x303} not in parent.
REJECT child: child raises max_progs above parent.
```

The user-space tool implements exactly the rule the kernel will
enforce on `MERLIN_NS_CREATE` (per `09-mvcp-kernel-uapi.md` §3.4):
any widen attempt fails with a precise diagnostic naming the
offending dimension.

## Scope-check at load time

Given a program's static facts (attach type it wants to use,
helper IDs it calls, kfunc IDs it references, memory it
consumes) and a namespace's current usage, `merlin-ns check`
returns either `ACCEPT (ns='...')` or
`REJECT reason=<name> offending_id=0xN`.  Reasons are:

```
attach_not_allowed
helper_not_allowed
kfunc_not_allowed
quota_progs
quota_maps
quota_map_mem
quota_prog_mem
sealed                  (reserved for future UPDATE blocking)
```

This is the same decision the kernel's `MERLIN_PROG_LOAD` will
make against the calling namespace.

## What's not yet implemented

- Trust-root keyring binding (`trust_keyring_id`).  The field is
  present in the canonical struct; the prototype loads / saves
  it as opaque but does not consult it.  Bound to the future
  `merlin-attest` integration with `merlin-sign`.
- `setns()` semantics (kernel concern).
- `merlinfs` per-namespace mount path (kernel concern).
- Namespace identifiers (`ns_id`): the prototype uses file
  paths; the kernel will assign monotonic IDs.
- `MERLIN_NS_F_VISIBLE_TO_CHILDREN` enforcement on
  `MERLIN_PROG_GET_NEXT_ID` (kernel concern).
- Live-update semantics; this prototype creates immutable
  configs.  The kernel's `MERLIN_NS_UPDATE` (with `SEALED`
  bit gating) is future work.

## Relationship to other tools

```
.merlin.o
   |  static analysis: which helpers? which attach type?
   v
merlin-verifier (extracts helper IDs from ecall sites)
   |
   v
program facts:  attach_type=1 (MVDP), helpers={0x110, 0x201}
   |
   |     namespace file:
   |        merlin-ns create --attach 1 --helpers 0x110,0x201
   |                         --max-progs 1 -o tenant_a.ns
   v
merlin-ns check --ns tenant_a.ns --attach 1 --helpers 0x110,0x201
   |
   v
ACCEPT   or   REJECT reason=... offending_id=...
```

In the eventual kernel implementation: extraction of program
facts happens inside the verifier; the scope check runs at
`MERLIN_PROG_LOAD` against the calling namespace's
`struct merlin_ns_config_v1`.  Algorithmically identical to
what this prototype demonstrates.
