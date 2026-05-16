# 10 — MVCP Layer B: `merlind` and fleet semantics

*Status: draft. The reference out-of-tree daemon and the fleet
protocol it implements on top of MVCP layer A.*

## 1. Purpose

[`09-mvcp-kernel-uapi.md`](09-mvcp-kernel-uapi.md) specifies the
in-kernel UAPI: signed programs, attestation, atomic map batches,
namespaces, telemetry, multicast control. Those primitives are
sufficient but inconvenient for fleet operators: at hyperscale you
do not want to teach every Kubernetes operator and SRE script how
to consume `MERLIN_NL_CTRL` and how to drive five-step rolling
upgrades.

**`merlind`** is the reference daemon — a single per-host process
— that exposes those primitives over a stable, language-agnostic
RPC surface (gRPC + HTTP/2; HTTP/1.1 + JSON for debugging) with
the fleet semantics most controllers actually want:

- rolling deploy across namespaces
- canary deploys with auto-rollback on telemetry breach
- blue/green deploys with explicit cutover
- centralised trust-root key management
- telemetry aggregation to Prometheus, OTLP, and arbitrary sinks
- multi-controller arbitration with a lease/lock model

`merlind` is **reference**, not mandatory. Any controller can
speak `merlin(2)` and `MERLIN_NL_CTRL` directly. Hyperscalers with
existing fleet pipelines (Cilium, Cloudflare, Meta) may keep their
own daemon and simply consume layer A — `merlind` does not gate
access to any kernel primitive.

## 2. Process model

One `merlind` per host. The process runs with:

- `CAP_MERLIN` in the root MERLIN namespace
- `CAP_NET_ADMIN` in the root netns (so it can install MVDP /
  XDP\_V / TC\_V links)
- Membership in the `merlind` multicast group on `MERLIN_NL_CTRL`
- A pinned `merlinfs` mount at `/run/merlin/`
- A read-only mount of the trust-root keyring path

It listens on:

- A local Unix domain socket `/run/merlind.sock` (default; gRPC + JSON)
- A configurable TCP socket (mTLS required; off by default)

Authentication for the TCP socket is mutual TLS with client
certificates issued by an org-managed CA. There is no anonymous
mode. The local UDS is gated by SO\_PEERCRED and a configurable
allowlist of uids/gids.

## 3. RPC surface

Service definition (gRPC; abbreviated for the design doc):

```
service MerlinFleet {
    /* Program lifecycle */
    rpc LoadProgram      (LoadProgramRequest)      returns (LoadProgramResponse);
    rpc ReplaceProgram   (ReplaceProgramRequest)   returns (ReplaceProgramResponse);
    rpc DetachProgram    (DetachProgramRequest)    returns (DetachProgramResponse);
    rpc ListPrograms     (ListProgramsRequest)     returns (ListProgramsResponse);

    /* Rolling / canary / blue-green */
    rpc StartDeployment  (DeploymentSpec)          returns (DeploymentHandle);
    rpc GetDeployment    (DeploymentHandle)        returns (DeploymentStatus);
    rpc PauseDeployment  (DeploymentHandle)        returns (DeploymentStatus);
    rpc ResumeDeployment (DeploymentHandle)        returns (DeploymentStatus);
    rpc CancelDeployment (DeploymentHandle)        returns (DeploymentStatus);

    /* Maps */
    rpc BatchUpdateMap   (BatchUpdateRequest)      returns (BatchUpdateResponse);
    rpc DumpMap          (DumpMapRequest)          returns (stream MapEntry);

    /* Namespaces */
    rpc CreateNamespace  (NamespaceSpec)           returns (NamespaceHandle);
    rpc DeleteNamespace  (NamespaceHandle)         returns (.google.protobuf.Empty);
    rpc ListNamespaces   (.google.protobuf.Empty)  returns (stream Namespace);

    /* Attestation */
    rpc Attest           (AttestRequest)           returns (AttestResponse);

    /* Trust root */
    rpc UpdateTrustRoot  (TrustRootUpdate)         returns (.google.protobuf.Empty);
    rpc ListTrustRoot    (TrustRootQuery)          returns (stream TrustedKey);

    /* Telemetry */
    rpc SubscribeTelemetry (TelemetryFilter)       returns (stream TelemetrySample);
}
```

The HTTP/JSON surface mirrors the gRPC surface 1:1 via gRPC-gateway.

### 3.1 LoadProgram

```
LoadProgramRequest {
    bytes  elf_blob;                /* MERLIN-V ELF; .merlin.sig required */
    string namespace;               /* MERLIN namespace name              */
    enum   prog_type;
    enum   attach_type;
    repeated string licence_check;  /* allowlist of acceptable licences   */
    map<string,string> labels;
}
LoadProgramResponse {
    string prog_id;                 /* opaque, stable across loads        */
    bytes  prog_tag;                /* SHA-256 of signed region           */
    string attestation_b64;         /* base64 of merlin_attestation_v1+sig*/
}
```

Internally `merlind` translates this to:

1. Resolve namespace name → ns\_fd (cached).
2. `MERLIN_KEYRING_BIND` if the namespace's trust root has changed.
3. `MERLIN_PROG_LOAD` with the ELF blob.
4. `MERLIN_PROG_GET_ATTESTATION` with a daemon-generated nonce.
5. Persist (prog\_id, prog\_tag, labels, attestation) to a
   small SQLite database under `/var/lib/merlind/`.

### 3.2 StartDeployment

A *deployment* is an opinionated wrapper over Load + Attach with
rollout policy:

```
DeploymentSpec {
    string  name;
    bytes   elf_blob;
    enum    strategy;            /* RECREATE | ROLLING | CANARY | BLUE_GREEN */
    Target  target;              /* netdev+queue range, or attach descriptor */
    Policy  policy;              /* see below                                */
}

Policy {
    google.protobuf.Duration min_observation;     /* before next step */
    repeated TelemetryGate   gates;               /* see §3.2.1       */
    bool                     rollback_on_breach;
    int32                    max_unhealthy_pct;
}

TelemetryGate {
    string  metric;              /* "verdict_count.drop_pct", "run_ns_p99" */
    enum    op;                  /* LT | GT | LT_EQ | GT_EQ                */
    double  threshold;
    google.protobuf.Duration window;
}
```

For a CANARY deployment with say 5 % traffic gating: `merlind`
installs the new program in `MVDP_DELIVER_VIA_MAP` mode on a
canary queue subset, watches its telemetry stream for the
configured window, and either promotes to the full set or rolls
back on gate breach. Rollback is implemented by atomic in-place
replace using `MVDP_PROG_ATTACH` with `current_revision` set
(see [`08-mvdp-and-af-mvdp.md`](08-mvdp-and-af-mvdp.md) §3.8.1).

### 3.3 BatchUpdateMap

A thin wrapper over the kernel-side transaction commands of
§3.3 of [`09-mvcp-kernel-uapi.md`](09-mvcp-kernel-uapi.md), with
two ergonomic additions:

- The request describes maps by name (resolved per namespace),
  not by fd.
- The daemon may declare a *coalescing window*: if multiple
  controllers submit overlapping updates within a configured
  window, `merlind` merges them into one kernel transaction.
  This is opt-in per request (`coalesce_window` field) because
  some controllers want strict per-request ordering.

## 4. Telemetry pipeline

`merlind` subscribes to `MERLIN_NL_CTRL`'s telemetry events on
boot. Samples are:

1. **Routed** to subscribers of `SubscribeTelemetry` matching the
   sample's namespace and labels.
2. **Aggregated** in-process per (prog\_id, label-set) into rate /
   percentile / counter rollups.
3. **Exported** to configured sinks. Three built-in sinks: a
   Prometheus `/metrics` endpoint, an OTLP/HTTP exporter, and a
   pluggable shell-out for custom sinks.

The aggregation window is fixed at 1 second; rollups for longer
windows are computed downstream by the receiver. This keeps
`merlind` itself memory-bounded — a long-uptime daemon never
accumulates unbounded state.

## 5. Multi-controller arbitration

A namespace may have multiple controllers that legitimately want
to deploy. `merlind` implements *lease-based* arbitration:

- A controller acquires a lease on (namespace, label-selector):
  `AcquireLease(ns, selector, duration) → lease_handle`.
- Mutating RPCs require a valid lease that covers the target.
- Telemetry / read RPCs do not require a lease.
- A lease may be renewed; expiry releases it without notification.
- A higher-priority lease can pre-empt a lower-priority lease,
  with the displaced controller notified via stream cancellation.

Lease state is held in `merlind` memory and persisted to disk on
acquire/release. Crash recovery re-loads outstanding leases and
sweeps any beyond their expiry.

Hyperscale fleet controllers (k8s operators, custom pipelines)
typically acquire a long-running lease covering the namespaces
they own; SRE break-glass tools acquire a short pre-empting lease
during incident response.

## 6. Reference implementation

Language: **Rust**. Rationale:

- Strong type system for the wide RPC + kernel-syscall surface.
- `rustix` provides clean wrappers for the rare kernel calls
  beyond what `libc` offers, including netlink generic.
- `tonic` for gRPC, `axum` for the HTTP/JSON gateway.
- The kernel-side prototype (`kernel/merlin/`) is C; userspace
  in Rust gives a meaningful diversity-of-implementation story
  for the test matrix.

Repository: **separate from this repo**. A new
`github.com/PacketFive/merlind` once the kernel UAPI prototypes
land. Until then `merlind` exists only as this design document.

Build outputs:

- `merlind` (the daemon)
- `merlinctl` (a CLI client speaking the gRPC surface)
- `libmerlin-client` (a thin client library; Go and C bindings
  ship as well)

## 7. Non-goals

Things `merlind` explicitly does **not** do:

- **Cluster-level orchestration.** That is the layer above
  `merlind`. A k8s operator, a Cilium agent, or a custom
  controller speaks to one `merlind` per host. `merlind` is
  per-host; orchestration is per-cluster.
- **Compile programs.** Programs arrive at `merlind` as signed
  ELF blobs. The build pipeline (clang / gcc / objtool) lives
  elsewhere; see [`04-toolchain.md`](04-toolchain.md).
- **Replace bpffs / merlinfs.** `merlind` *uses* merlinfs for
  persistent pinning; it does not abstract over the filesystem.
  Tools that bypass `merlind` and write directly to merlinfs
  remain valid.
- **Federate.** Multi-host coordination is the orchestrator's
  job. `merlind`'s view ends at the host boundary. The closest
  thing to federation is the multicast telemetry sink, which is
  one-way and read-only.

## 8. Open items

- **Persistent state format.** SQLite vs. flat JSON files for
  `/var/lib/merlind/`. v0 leans SQLite for transactional updates
  and lease state.
- **Lease pre-emption semantics.** Whether pre-emption fires a
  best-effort detach-on-pre-empt or only invalidates future
  writes. v0 leans "invalidate future writes; do not touch
  existing program state."
- **OTLP push vs. pull.** The OTLP exporter ships push by
  default; pull adds complexity for marginal benefit at typical
  hyperscale scrape intervals.
- **Whether the CLI (`merlinctl`) and the daemon (`merlind`)
  share a process / repo with the kernel-side `merlin-objtool`.**
  Current plan: separate repos, separate release cycles,
  shared schema crate (`merlin-uapi-rs`).
