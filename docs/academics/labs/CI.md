# MERLIN-V Labs — CI Autograder

> **TL;DR.** Push to a lab, get graded. The autograder runs in
> GitHub Actions (or Azure DevOps) on every push or PR, overlays the
> canonical instructor files on top of your tree so you can't game
> the test suite, runs the lab's tests, and publishes a Markdown
> grade summary as a PR comment + workflow artifact.

---

## How it works

```
   student push / PR
          │
          ▼
   ┌──────────────────────────┐
   │  CI runner (GH or ADO)   │
   │                          │
   │  1. checkout student     │
   │  2. checkout instructor  │  ← canonical tree from upstream
   │  3. changed-labs.sh      │  ← detect which labs to grade
   │  4. instructor-overlay   │  ← replace tests/, Makefile,
   │                          │     PROVIDED headers
   │  5. runner.sh per lab    │  ← build + make test
   │  6. ci-comment.py        │  ← JSON → Markdown
   │  7. publish artifacts    │
   │  8. comment on PR        │
   └──────────────────────────┘
```

The four scripts that make this work are all under
`docs/academics/labs/autograder/`:

| Script | Role |
|--------|------|
| `runner.sh` | Drives a single lab: builds, tests, emits JSON |
| `ci/changed-labs.sh` | git diff → list of lab IDs to grade |
| `ci/instructor-overlay.sh` | Replace student's test files with canonical |
| `ci/ci-comment.py` | JSON manifest → Markdown PR comment |

The two pipeline definitions:

| File | Platform |
|------|----------|
| `.github/workflows/autograde.yml` | GitHub Actions (push + PR) |
| `.github/workflows/autograde-reusable.yml` | Reusable workflow for student forks |
| `azure-pipelines/autograde.yml` | Azure DevOps Pipelines |

---

## Academic integrity

**The autograder runs against an overlay tree, not your raw push.**

When the pipeline starts, it checks out two copies:

- `student/`     — your push
- `instructor/`  — the canonical upstream tree (a locked-down branch
                   the instructor controls)

Then for each changed lab, the `instructor-overlay.sh` script copies
the following from `instructor/` over `student/`:

- The lab's `Makefile`
- The entire `tests/` directory
- Any header marked `/* PROVIDED — do not modify */`
- The lab's autograder manifest (`autograder/lab-NN.yml`)
- The shared `common/` directory (`elf.h/c`, `rv32.h`, `test_helpers.h`)

What stays from your push:

- `src/*.c` files with `TODO` blocks — those are what you implement.
- `README.md` — yours to annotate.
- `exercises.c` (lab-02) — your encoding answers.

**Consequence:** there is no point in modifying tests, Makefile, or
provided headers in your submission. Those files are silently
overwritten before grading.

If you genuinely believe a test is wrong (off-by-one in the
expected output, unreasonable timing requirement, etc.), file an
issue against the upstream repo. Do not "fix" the test in your fork
— it will be overlaid back to the canonical version on the next
graded push.

---

## Setting up CI in a student fork (GitHub)

Add a single file to your fork at `.github/workflows/grade.yml`:

```yaml
name: grade
on:
  push:
    paths: ['docs/academics/labs/**']
  workflow_dispatch:

jobs:
  grade:
    uses: PacketFive/MERLIN-V/.github/workflows/autograde-reusable.yml@main
    with:
      instructor_repo: PacketFive/MERLIN-V
      instructor_ref:  semester-2026     # ask your instructor
```

That's it. Every push to the fork that touches a lab kicks off the
grader. The result appears as:

- A green/red check next to the commit in GitHub UI.
- A grade table in the workflow's Job Summary.
- A `grades.jsonl` artifact you can download (and the instructor's
  CI consumes for the gradebook).

---

## Setting up CI in a student fork (Azure DevOps)

1. Create a new pipeline in your ADO project, pointing at the
   `azure-pipelines/autograde.yml` file inside this repo.
2. In the pipeline's **Variables** tab, set:
   - `INSTRUCTOR_REPO_URL` — e.g.
     `https://github.com/PacketFive/MERLIN-V.git`
   - `INSTRUCTOR_REF` — e.g. `semester-2026`
3. Grant the build identity "Contribute to pull requests" on the
   target repository (so the PR-comment step can post).
4. Trigger a build by pushing a lab change or by clicking
   "Run pipeline".

---

## Instructor workflow

The instructor controls one branch on the upstream repo — typically
`semester-NNNN` (e.g. `semester-2026`). That branch is what the
overlay step copies *from*. To roll out a fix mid-semester:

```bash
git checkout semester-2026
# edit the canonical test or PROVIDED file
git commit -s
git push origin semester-2026
# all subsequent student pushes are graded against the new files
```

Students do not need to do anything; the next push to their fork
picks up the new instructor files automatically.

### Aggregating grades across all students

Each successful pipeline run uploads a `grades.jsonl` artifact. To
collect them into a gradebook, run a periodic job that:

1. Lists all forks of the repo (`gh api ... /forks`).
2. For each fork, fetches the latest successful workflow run on the
   `grade.yml` workflow.
3. Downloads its `autograder-results-*` artifact.
4. Concatenates the `grades.jsonl` files, keyed by fork owner.

A skeleton implementation lives at
`docs/academics/labs/autograder/ci/aggregate-grades.py` (TODO —
this script is planned for the next release; instructors who want
it sooner can adapt
[github-classroom-grader](https://github.com/education/classroom)
patterns).

### Tightening the gate

By default the workflow does not fail on a low score — it just
publishes the grade. To make a low score fail the CI run (so
instructors who use CI status as the rubric can rely on it), pass
`hard_gate: true` from the calling fork's workflow:

```yaml
jobs:
  grade:
    uses: PacketFive/MERLIN-V/.github/workflows/autograde-reusable.yml@main
    with:
      instructor_repo: PacketFive/MERLIN-V
      instructor_ref:  semester-2026
      hard_gate:       true
```

---

## Local re-grading

The autograder runs locally too — exactly the same code path as the
CI does, minus the overlay:

```bash
docs/academics/labs/autograder/runner.sh lab-03 | \
    docs/academics/labs/autograder/score.py lab-03
```

To simulate the CI's overlay step locally (useful when debugging a
disagreement between your local result and the CI result):

```bash
# In a separate directory:
git clone https://github.com/PacketFive/MERLIN-V instructor-checkout
cd /path/to/your/fork
docs/academics/labs/autograder/ci/instructor-overlay.sh \
    lab-03 /absolute/path/to/instructor-checkout
docs/academics/labs/autograder/runner.sh lab-03
```

---

## What the JSON looks like

One record per lab, JSON object, one per line in `grades.jsonl`:

```json
{"lab":"lab-03","build":"ok","pass":7,"total":7,"score":100}
{"lab":"lab-04","build":"ok","pass":6,"total":11,"score":55}
```

Fields:

| Field | Meaning |
|-------|---------|
| `lab` | Lab ID, e.g. `lab-03` |
| `build` | `ok` or `fail` (compile errors → `fail`) |
| `pass` | Number of test cases passed (omitted on build fail) |
| `total` | Number of test cases run (omitted on build fail) |
| `score` | 0-100 integer score |

The Markdown rendered by `ci-comment.py` is a convenience layer
over these JSON records. Instructors integrating with an external
gradebook should consume the JSON directly.

---

## Limitations

- **Skeleton labs only.** Labs 05, 06, 07 ship as stub READMEs (they
  point at the parent project's user-space prototypes). The
  autograder skips them quietly. The reusable workflow's hard-gate
  doesn't fire on a lab with no tests.

- **No HW-in-the-loop yet.** Labs 08 (kernel module) and 09 (Zephyr
  runtime) require a board to fully exercise. The CI does the
  software-only path (host smoke tests) and leaves the HW path to
  manual grading.

- **Time-bounded.** The default GitHub Actions / ADO timeout is 15
  minutes per job. The current lab autograders complete in <30s; a
  fuzz-test extension would need a different runner.

- **The PR-comment step needs write permissions.** On GitHub the
  default `GITHUB_TOKEN` has these for in-repo PRs but not for PRs
  from forks. Either:
  - have students push to branches in the upstream repo (admin sets
    branch protection so they can't merge), or
  - use `pull_request_target` (security-careful) for cross-fork PRs.

---

## Assisted-by

Copilot-CLI:Claude-Opus
