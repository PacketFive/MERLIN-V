# Lab 11 — Capstone: XDP-V Drop with Measurements

Module: 7
Effort tier: L
Prerequisites: Labs 04, 06 (or 07), 08.
Design docs: [`docs/design/03-kernel-interfaces.md`](../design/03-kernel-interfaces.md),
[`docs/design/07-jit-and-offload.md`](../design/07-jit-and-offload.md).

## Goal

Ship a *small but complete* demonstrator that:

1. Loads a BPF-V program into your Lab 08 kernel module.
2. Attaches it as a packet filter on a `veth` interface (a
   reduced-scope stand-in for XDP).
3. Counts and optionally drops packets matching a programmable
   filter expressed in BPF-V.
4. Reports verifier time, load-to-run latency, per-packet cycles,
   and a packet-throughput number compared against a baseline
   ("kernel runs no BPF-V"; "kernel runs an empty BPF-V program";
   "kernel runs the real filter").

The capstone writeup is structured like a workshop-paper section.

## Required components

- A new ioctl in `bpfvi.ko` from Lab 08:
  `BPFVI_IOC_ATTACH_VETH(prog_id, ifindex)`.
- A small netfilter hook (initial path; XDP-on-veth has well-known
  caveats and is *not* required for this capstone) that, on each
  ingress `sk_buff`, calls the loaded program with a context
  describing the packet (data pointer, length).
- A user-space tool `bpfvi-xdpv` that loads a program, attaches it,
  prints counters, and detaches.
- Three demonstrator programs:
  1. `dropall.bpfv` — returns `XDP_V_DROP`.
  2. `dropv4.bpfv` — returns `DROP` if EtherType==IPv4, else `PASS`.
  3. `count.bpfv` — increments a map entry, always returns `PASS`.

(Using netfilter rather than full XDP makes this implementable in a
single capstone; the design conversation about the *real* XDP-V hook
belongs in the writeup, not the code.)

## Measurements

For each demonstrator:

1. **Verifier time** — wall-clock ns from `BPFVI_IOC_LOAD` start to
   verifier-accepted, measured by user-space `clock_gettime`.
2. **Load-to-run latency** — wall-clock ns from `BPFVI_IOC_LOAD`
   start to the first packet hitting the program.
3. **Per-packet cost** — measure with a 64-byte UDP storm into the
   `veth` peer, comparing:
   - Baseline (no BPF-V attached).
   - Empty BPF-V (`return PASS`).
   - The demonstrator.
   Report packets per second and CPU time per packet.
4. **Code size** — strip the demonstrator's `.text` and report bytes
   for source-of-truth.

The lab provides a `tools/bench/` harness that runs `pktgen` and
captures stats.

## Writeup structure (workshop-paper style)

Use this exact section layout in `WRITEUP.md`:

1. **Abstract** — 150 words.
2. **Introduction** — 1 page.
3. **Background** — eBPF and BPF-V, ≤ 1 page.
4. **Design** — what you built, ≤ 2 pages.
5. **Implementation** — files, line counts, gotchas, ≤ 2 pages.
6. **Evaluation** — measurements, tables/plots, ≤ 2 pages.
7. **Discussion** — what surprised you, ≤ 1 page.
8. **Related work** — eBPF, RISC-V offload, NIC firmware VMs,
   ≤ 0.5 page.
9. **Conclusion and future work** — ≤ 0.5 page.
10. **AI assistance statement** — explicit `Assisted-by:` per project
    policy, plus a short paragraph explaining what was AI-assisted
    and what was not.

Target total: 8–10 pages. Use the ACM `acmart` template if you have
LaTeX handy, otherwise a plain-text/Markdown writeup is acceptable
for grading (the autograder cares about substance, not formatting).

## Deliverables

- `kmod/bpfvi.ko` extended with `BPFVI_IOC_ATTACH_VETH`.
- `user/bpfvi-xdpv/` CLI.
- The three demonstrator programs under `samples/`.
- `tools/bench/` reproducible benchmark harness.
- `WRITEUP.md` (or `WRITEUP.pdf` if you used LaTeX) following the
  section structure above.
- `RESULTS.csv` machine-readable measurements.

## Rubric

| Criterion | Points |
| --------- | ------ |
| Kernel hook reliable; no panics under load | 10 |
| Three demonstrator programs all correct | 15 |
| Verifier-time and load-latency numbers reported with methodology | 15 |
| Per-packet cost numbers vs baseline reported with units | 20 |
| Benchmark harness is reproducible (`tools/bench/run.sh` works) | 10 |
| Writeup follows the section structure and is substantive | 20 |
| Related work is accurate (citations correct, eBPF positioning fair) | 5 |
| AI assistance statement complete | 5 |
| **Total** | **100** |

## Optional ambition (bonus, up to +20)

Any one of:

- Implement the demonstrator's hook as a real eBPF/XDP-V coexistence
  path: an XDP-V program attached via the kernel's XDP machinery,
  with proper `XDP_V_PASS/DROP/REDIRECT` semantics.
- Run the same demonstrator unmodified on Zephyr (Lab 09) against a
  software packet source, and compare the verifier-time + load-time
  to the Linux numbers.
- Demonstrate a BPF-V program built with **Clang** and one built with
  **GCC** producing different verifier behaviour and explain why.
- Show a real-hardware run on Lab 10's board, with a synthetic packet
  source over the board's network interface.

## Common pitfalls

- Measuring throughput on a `veth` pair without pinning CPUs and
  IRQs: numbers become noise.
- Forgetting to subtract baseline measurement variance.
- Claiming microbenchmark numbers without showing variance and run
  count.
- Conflating verifier time with JIT/relocation time. They are
  separate budgets and should be reported separately.

## What's next (course exit)

You now have:

- An implemented mental model of every box in
  `docs/design/00-overview.md`.
- A working user-space and in-kernel BPF-V stack.
- A demonstrator and a writeup suitable for a workshop submission.

Next directions:

- Push the verifier toward eBPF-verifier-shared infrastructure
  (Option A in `docs/design/06-verifier.md` §4).
- Build the offload path (Phase 2: real RISC-V SmartNIC or PolarFire
  fabric soft-core, see
  `docs/design/05-reference-platforms.md` §1.3).
- Take a piece upstream as an RFC patch, with your name attached.
