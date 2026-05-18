# MERLIN-V UAPI Headers (draft)

This directory holds the canonical draft of the MERLIN-V UAPI in
two halves:

| Subdir | Audience | Eventual in-tree path |
| ------ | -------- | --------------------- |
| [`linux/`](linux/)   | kernel + userspace loaders | `include/uapi/linux/` |
| [`merlin/`](merlin/) | MERLIN-V programs (C source) | `tools/lib/merlin/include/merlin/` |

The split mirrors how the eBPF tree organises BPF: kernel UAPI in
`include/uapi/linux/bpf.h`, program-side headers in
`tools/lib/bpf/include/bpf/*.h`.

| Draft path | Eventual in-tree path |
| ---------- | --------------------- |
| `linux/merlin.h`           | `include/uapi/linux/merlin.h`        |
| `linux/if_mvdp.h`          | `include/uapi/linux/if_mvdp.h`       |
| `merlin/types.h`           | `tools/lib/merlin/include/merlin/types.h` |
| `merlin/helpers.h`         | `tools/lib/merlin/include/merlin/helpers.h` |
| `merlin/maps.h`            | `tools/lib/merlin/include/merlin/maps.h` |
| `merlin/section_macros.h`  | `tools/lib/merlin/include/merlin/section_macros.h` |
| `merlin/license.h`         | `tools/lib/merlin/include/merlin/license.h` |
| `merlin/core.h`            | `tools/lib/merlin/include/merlin/core.h` |
| `merlin/stats.h`           | `tools/lib/merlin/include/merlin/stats.h` |
| `merlin/attestation.h`     | `tools/lib/merlin/include/merlin/attestation.h` |
| `merlin/namespace.h`       | `tools/lib/merlin/include/merlin/namespace.h` |
| `merlin/mvdp.h`            | `tools/lib/merlin/include/merlin/mvdp.h` |
| `merlin/merlin.h`          | `tools/lib/merlin/include/merlin/merlin.h` |

## Source-of-truth ordering

- These headers are the source of truth for the **wire format** of
  `union merlin_attr`, MVDP/AF\_MVDP, command and constant
  enumerations.
- The Markdown documents in [`../`](../) (00–08) are the source of
  truth for **rationale**, design alternatives considered, open
  questions, and cross-references between subsystems.
- When a number, layout, or flag bit disagrees between a `.md` and a
  `.h` in this directory, **the `.h` wins** and the `.md` is the bug.

## Stability

Everything in this directory is DRAFT.  Before the RFC v1 patch
series is posted, anything may change.  Once posted, the layouts are
frozen by the standard Linux UAPI rules: append-only fields in
zero-initialised tail regions, new constants added at the end of
their enumerations, no renumbering, no field reordering, no removals.

## Coordination asks (visible at draft time)

- `AF_MVDP` and `PF_MVDP` — request a number from `include/linux/socket.h`.
- `SOL_MVDP` — request a number from the SOL\_* namespace.
- New errno (or shared with eBPF) for "no MVDP program at bind target"
  — see [`../08-mvdp-and-af-mvdp.md`](../08-mvdp-and-af-mvdp.md) §3.6.

These items are flagged in the headers with placeholder values and
`/* TBD */` comments.

## Out-of-tree build use

External tooling and prototype kernel modules may include these
headers directly during the design phase.  Once the in-tree headers
land, projects switch to `<linux/merlin.h>` and `<linux/if_mvdp.h>`
with no source changes.
