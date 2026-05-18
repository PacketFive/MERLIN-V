# 13 — Evaluation Plan

Status: starter
Owner: PacketFive
Last reviewed: initial draft

> **Note on numbering.** The original todo named this `08-evaluation-plan.md`;
> slot 08 was subsequently consumed by `08-mvdp-and-af-mvdp.md` (MVDP
> data path).  This document lives at slot 13 and is the canonical
> evaluation plan.

This document specifies **how MERLIN-V will be measured against its
hypothesis** — i.e. what we will count, on what hardware, against what
baseline, and what number constitutes "success" for the RFC v1
acceptance bar.

The evaluation plan is intentionally written *before* the
implementation matures, so we cannot move the goalposts.

---

## 1. The hypothesis (compact form)

> Reusing the RISC-V ISA as in-kernel JIT VM bytecode preserves eBPF's
> safety properties while reducing the per-host translation cost on
> RISC-V hosts to zero, and on non-RISC-V hosts to "approximately the
> same as eBPF's per-arch JIT," at the cost of a new verifier and a
> new patch series.

Three operational sub-claims fall out:

- **S1. Verifier parity.** A MERLIN-V verifier verdict on a program
  matches the eBPF verifier's verdict on the semantically-equivalent
  eBPF program (modulo profile-specific feature differences).
- **S2. Pass-through wins on RISC-V.** Verify-time and load-time on a
  RISC-V Linux host are strictly less than the verify-time + JIT-time
  of the equivalent eBPF program.
- **S3. Host-JIT parity off RISC-V.** Verify-time and JIT-time on
  x86_64 Linux are within 2× of the equivalent eBPF path for
  programs of comparable size and complexity.

Each is measurable. Each has a pass/fail criterion below.

---

## 2. Reference workloads

We define five workloads spanning the prog-type space MERLIN-V
targets in Phase 1.  Every metric is measured against all five.

| ID | Program type | Description | Approx insn count |
|----|--------------|-------------|-------------------|
| W1 | XDP-V / MVDP | Ethernet + IPv4 5-tuple parse → drop/pass | 40–80 |
| W2 | XDP-V / MVDP | IPv4 + TCP + LPM lookup against allowlist | 100–200 |
| W3 | TC-V         | Mark/drop based on cgroup hash map         | 60–120 |
| W4 | Tracepoint   | Argument capture + ringbuf submit          | 30–60 |
| W5 | Filter (RTOS)| EtherType classifier (the C3 sample)       | 7–20 |

Each workload is implemented twice: once in **C → MERLIN-V**
(`riscv32/64-unknown-elf-gcc` + section macros) and once in **C → eBPF**
(`clang -target bpf`).  Both produce a `.merlin.o` / `.bpf.o` checked
into `tools/testing/selftests/merlin/workloads/`.

The workloads are deliberately representative, not maximal.  The plan
is not to argue MERLIN-V is faster than eBPF in the limit — it is to
argue MERLIN-V is **competitive** in the limit and **structurally
simpler** on the RISC-V case.

---

## 3. Measurement environments

| Env | Host ISA | OS | Kernel | Role |
|-----|----------|----|--------|------|
| E1 | x86_64 (Xeon-class) | Linux 6.x | bpf-next + MERLIN-V series | non-RISC-V host JIT baseline |
| E2 | aarch64 (Pi 4B) | Linux 6.x | bpf-next + MERLIN-V series | second non-RISC-V host JIT |
| E3 | RV64GC (MPFS Icicle Kit) | Linux 6.x | bpf-next + MERLIN-V series | RISC-V pass-through |
| E4 | RV32IMC (ESP32-C3) | Zephyr | Zephyr LTS + zephyr/merlin | RTOS profile, no JIT |
| E5 | RV32IMC (FPGA soft core) | bare metal | — | offload demonstrator (future) |

E1 is the CI baseline.  E2 and E3 are physical-board-required; CI
runs are scheduled (not per-commit).  E4 runs in Zephyr's Twister
harness on real hardware in the lab; smoke is also possible via the
QEMU `esp32c3_devkitm` emulation.

Each measurement is **30 runs, median + IQR reported**, taken after
3 warm-up runs and with the CPU pinned to a single core.

---

## 4. Metrics

### 4.1 Verifier metrics

For each (workload × environment) pair:

| Metric | Unit | Source |
|--------|------|--------|
| `verify_time_us` | microseconds | `kvtime_ns` deltas in `merlin_prog_load` |
| `verify_insns` | count | verifier counter |
| `verify_state_walks` | count | (Phase 2; not in v1) |
| `verify_log_bytes` | bytes | length of log buffer used |
| `accept_or_reject` | boolean | `MERLIN_VERIFY_{OK,REJECT}` |

Counterpart for eBPF: pull from `bpf_prog_info.verified_insns` and a
kprobe on `bpf_check`.

### 4.2 Load / JIT metrics

| Metric | Unit | Source |
|--------|------|--------|
| `load_time_us` | microseconds | full `MERLIN_PROG_LOAD` round-trip |
| `jit_time_us` | microseconds | `merlin_jit_ops.translate` deltas |
| `jit_image_bytes` | bytes | `struct merlin_jit_image.code_len` |
| `bytecode_bytes` | bytes | text section size |

Counterpart for eBPF: `BPF_PROG_LOAD` round-trip, `bpf_prog_info.
jited_prog_len`.

### 4.3 Run-path metrics

| Metric | Unit | Source |
|--------|------|--------|
| `invoke_time_ns` | nanoseconds | `MERLIN_PROG_TEST_RUN` `duration_ns` |
| `pps` | packets/sec | DPDK pktgen or `xdp_redirect_user` for XDP-V/MVDP |
| `recursion_misses` | count | `atomic64_t` in `struct merlin_prog` |

Counterpart for eBPF: `BPF_PROG_TEST_RUN`, `BPF_PROG_RUN`.

### 4.4 Footprint metrics

| Metric | Unit | Source |
|--------|------|--------|
| `runtime_text_bytes` | bytes | `size` on `merlin.ko` / Zephyr `merlin` lib |
| `runtime_rodata_bytes` | bytes | ditto |
| `runtime_bss_kbytes_per_load` | KiB / loaded prog | `slabinfo` delta or Zephyr heap delta |

---

## 5. Pass / fail criteria

### S1 — Verifier parity (per workload, per environment)

> The MERLIN-V verifier accepts a workload iff the eBPF verifier
> accepts the eBPF-port of the same workload, **modulo a documented
> list of profile differences** (e.g. C-extension instructions
> rejected in Phase 1; bounded loops require explicit helper).

- **Pass:** for every workload W1–W5, the accept/reject verdict
  matches across MERLIN-V and eBPF on E1.
- **Soft fail:** divergence is allowed only if it is documented in
  `docs/design/06-verifier.md §7` and the documented reason applies.
- **Hard fail:** undocumented divergence in either direction.

### S2 — Pass-through wins on RISC-V (E3)

> `load_time_us(MERLIN-V on E3, W*) ≤ load_time_us(eBPF on E3, W*)`
> with a meaningful margin.

- **Pass:** median MERLIN-V load is at least **30% faster** than
  median eBPF load on the same workload on E3, with non-overlapping
  IQRs at the 30-run sample.
- **Soft fail:** 0–30% faster (still a win, but the margin is
  smaller than the design predicts; investigate why).
- **Hard fail:** MERLIN-V load is slower than eBPF on E3.  The whole
  pass-through claim requires investigation.

### S3 — Host-JIT parity off RISC-V (E1, E2)

> MERLIN-V's load-time on a non-RISC-V host is within 2× of eBPF's
> load-time for the same workload.

- **Pass:** `load_time_us(MERLIN-V) ≤ 2 × load_time_us(eBPF)` median
  for every workload on E1 and E2.
- **Soft fail:** 2× < ratio ≤ 4×.  Acceptable for RFC v1 if a clear
  optimisation path is identified (e.g. register allocator, branch
  patching).
- **Hard fail:** ratio > 4×.  The host-JIT prototype is not
  competitive enough to ship even as RFC.

### Footprint criteria

| Target | Cap | Rationale |
|--------|-----|-----------|
| Runtime text+rodata on E4 | ≤ **40 KiB** | C3 has 400 KiB SRAM; runtime + Zephyr + app fits |
| `runtime_text_bytes` on E1 | ≤ **150 KiB** | Comparable to eBPF JIT + verifier infrastructure |
| Per-load BSS (any env)    | ≤ **8 KiB / prog** | static slot table + small allocations |

---

## 6. Harness implementation

Phase 1 harness lives under `tools/testing/selftests/merlin/`:

```
tools/testing/selftests/merlin/
├── workloads/
│   ├── w1_xdp_5tuple/                 *.c (twice: merlin + bpf)
│   ├── w2_xdp_lpm_allowlist/
│   ├── w3_tc_cgroup_hash/
│   ├── w4_trace_ringbuf/
│   └── w5_rtos_etype_classifier/
├── runner/
│   ├── run_one.c              loads, measures, prints JSON
│   ├── run_all.sh             iterates over workloads × runtimes
│   └── analyse.py             reads JSON, computes median+IQR
└── ci/
    ├── e1_x86_64.yml          GH Actions schedule (per-PR)
    ├── e2_aarch64_pi.yml      GH Actions on self-hosted Pi runner
    └── e3_riscv_icicle.yml    GH Actions on self-hosted Icicle runner
```

The runner emits one JSON record per measurement; `analyse.py`
reduces them to the median/IQR tables that appear in the RFC.

### 6.1 Single-record JSON schema

```json
{
  "env":          "E1",
  "host_isa":     "x86_64",
  "runtime":      "merlin",
  "workload":     "w1_xdp_5tuple",
  "verify_time_us":   123.4,
  "verify_insns":     147,
  "load_time_us":     456.7,
  "jit_time_us":      210.5,
  "jit_image_bytes":  2048,
  "bytecode_bytes":   588,
  "invoke_time_ns":   72,
  "run":              17,
  "ts_unix":          1731234567
}
```

Comparable records are emitted for `runtime: "bpf"`.

---

## 7. Reporting

The RFC cover letter will carry **one summary table** per claim
(S1, S2, S3) and **one footprint table**, computed from at least
30 runs per cell on E1/E2/E3, plus a single boot log from E4.

Raw JSON is checked in under
`tools/testing/selftests/merlin/results/v1/` so reviewers can rerun
`analyse.py` themselves.

---

## 8. What we are deliberately not measuring (yet)

- **Throughput on full XDP / DPDK packet streams.** This is the
  natural follow-on but requires production-shape NIC drivers and
  packet generators; deferred to v2.
- **Tail-latency under contention.** Deferred to v2.
- **Memory safety bug-bounty results.** The verifier ships with
  property tests (`tools/merlin-verifier`); a full fuzzing campaign
  is planned for after RFC v1 lands.
- **Power on the C3 / Icicle.** Out of scope for RFC v1; tracked
  separately as part of the academic-research strand
  (KESTREL-V, doctoral work).

---

## 9. Open questions for reviewers

- Is "within 2× of eBPF on x86_64" the right host-JIT bar?  We could
  argue for 1.5× given that MERLIN-V skips a bytecode-translation
  step on the eBPF side.
- The pass-through win on RISC-V is **structural** (no translation).
  Should we measure it at all, or just argue it from first
  principles and report the load-time number as a confirmation?
- For W4 (tracepoint + ringbuf), the ringbuf submit dominates
  measurement noise; a different micro-benchmark might be needed.

---

## 10. Acceptance summary for RFC v1

The series is ready to post when:

- [ ] All five workloads exist in both runtimes.
- [ ] S1 (verifier parity) shows zero undocumented divergences on E1.
- [ ] S2 (pass-through win on E3) clears the **soft-fail** bar at
      minimum — i.e. MERLIN-V is at least as fast as eBPF, ideally
      30%+ faster.
- [ ] S3 (host-JIT on E1) clears the **soft-fail** bar at minimum.
- [ ] Footprint caps are met on E4 (the strictest target).
- [ ] All measurement JSON checked in for v1.

The cover letter in `docs/rfc/cover-letter.md` (local only) cites
the resulting tables.

---

## Assisted-by

Copilot-CLI:Claude-Opus
