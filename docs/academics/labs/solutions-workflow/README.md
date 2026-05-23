# Solutions Distribution Workflow

> **Status.** Workflow specification + reference solutions packaging
> instructions.  This directory is **public**.  The actual reference
> solution sources are **not in this repository**; they live on a
> private mirror per the distribution model below.

## The model

Reference solutions for the autograded labs (Lab 02, 03, 04 in v1;
later labs as they are added) are maintained on a separate private
repository:

```
PacketFive/MERLIN-V-solutions   (private; instructor access only)
```

The public `PacketFive/MERLIN-V` repo (this one) ships:

- The lab statements (`docs/academics/lab-*.md`).
- The student-facing skeleton code (`docs/academics/labs/lab-*/`).
- The autograder (`docs/academics/labs/autograder/`).
- The CI pipelines (`.github/workflows/`, `azure-pipelines/`).
- This workflow document.

It does **not** ship the filled-in TODO blocks.  Students see exactly
what they need to implement; instructors who request access get the
private mirror with the reference solutions filled in.

## Why a separate private repo

Three options were considered:

| Option | Pros | Cons | Decision |
|---|---|---|---|
| Protected branch on the public repo | Single repo to maintain | Branch protection is bypassable; private fork would expose history | Rejected |
| Solutions in `.gitignore`'d directory | Trivial to maintain | Easy to commit accidentally; no review process | Rejected |
| Separate private repo | Clean separation; access control via GitHub teams; PR review on solutions; CI can autograde against solutions | Two-repo maintenance | **Adopted** |

The two-repo cost is small: the solutions repo's CI runs the same
autograder this directory ships, against the filled-in solutions,
and confirms 100/100 on every lab.  A drift between the two repos
shows up as autograder failure in CI on the solutions repo.

## Directory layout (mirror)

The private repo mirrors this public repo's lab tree exactly,
with `TODO_ENCODING` and `TODO #N` markers replaced by the
reference answers:

```
PacketFive/MERLIN-V-solutions/
├── README.md
└── labs/
    ├── common/                 # identical to public
    ├── lab-02-riscv-isa-primer/
    │   ├── README.md           # identical
    │   ├── Makefile            # identical
    │   └── exercises.c         # TODO_ENCODING replaced with answers
    ├── lab-03-userland-interp/
    │   ├── README.md           # identical
    │   ├── Makefile            # identical
    │   ├── src/
    │   │   ├── interp.h        # identical (PROVIDED)
    │   │   ├── decode.c        # identical (PROVIDED)
    │   │   ├── main.c          # identical (PROVIDED)
    │   │   └── interp.c        # TODO #1-#5 implementations
    │   └── tests/              # identical
    └── lab-04-minimal-verifier/
        ├── README.md           # identical
        ├── Makefile            # identical
        ├── src/
        │   ├── verify.h        # identical (PROVIDED)
        │   ├── main.c          # identical (PROVIDED)
        │   └── verify.c        # TODO #1-#5 implementations
        └── tests/              # identical
```

`solutions/` files override `labs/` files; everything else is a
1:1 symlink or rsync from the public lab tree at solutions-repo
update time.

## Reference solution sources

The reference implementations are mechanically derivable from
existing code in this public repository:

| Lab | Reference source | Notes |
|---|---|---|
| Lab 02 | The expected_encoding values already in `exercises.c` | Trivial: copy the expected value into the TODO slot |
| Lab 03 | `kernel/merlin/dispatch.c` (helper handlers) + standard RV32I semantics | The interpreter dispatch matches the kernel's helper handling |
| Lab 04 | `tools/merlin-verifier/verify.c` (full impl) | Direct port of the userland verifier algorithm |

Instructors can produce the solutions repo by:

```bash
# 1. Fork or clone the public repo as a private repo
git clone --bare https://github.com/PacketFive/MERLIN-V.git
cd MERLIN-V.git
git push --mirror git@github.com:PacketFive/MERLIN-V-solutions.git
cd ..
git clone git@github.com:PacketFive/MERLIN-V-solutions.git
cd MERLIN-V-solutions

# 2. Fill in the TODO blocks per the reference-implementation crib
#    in this directory.  See REFERENCE-CRIB.md.

# 3. Run the autograder on the filled-in tree
docs/academics/labs/autograder/runner.sh lab-02 | \
    docs/academics/labs/autograder/score.py lab-02
# Expect: 100 / 100  Grade: A

# 4. Set up branch protection: nobody pushes to main without PR review.
```

## Access control

The private repo is restricted to a GitHub Team named `instructors`.
Adding an instructor:

1. Verify the instructor's institutional email (must match a known
   academic institution).
2. Verify they have signed the instructor honour-code statement
   (template in `INSTRUCTOR-AGREEMENT.md`).
3. Add to the GitHub team with `read` permission (no push).
4. Push permissions are restricted to the project owner; instructors
   submit improvements via PR.

## CI on the solutions repo

The solutions repo runs the same `.github/workflows/autograde.yml`
this repo ships.  Because the solutions tree fills in all TODO
blocks, every push to `main` of the solutions repo produces:

```
| Lab     | Build | Tests | Score | Grade |
| lab-02  | ✅ ok | 6/6   | 🟢 100 | A     |
| lab-03  | ✅ ok | 7/7   | 🟢 100 | A     |
| lab-04  | ✅ ok | 11/11 | 🟢 100 | A     |
```

A drop below 100/100 on any lab in CI means the solutions are out
of sync with the autograder's expected outputs --- usually because
a test was added to the public repo and the solutions repo hasn't
caught up.  This is the maintenance signal that the solutions need
attention.

## Requesting access

Instructors at accredited institutions can request access via:

```
academics@packetfive.com
Subject: MERLIN-V solutions access request
Body: institution, role, course code, expected enrollment, brief
      note on intended use.
```

Turnaround is typically a few business days.

## What is NOT in the solutions repo

- Hidden test cases the autograder uses but doesn't ship to
  students.  Those live in a third private repo, accessible only
  to the project owner.  This is the standard ``autograder
  trapdoor'' pattern: solutions match the visible test set
  precisely, but the autograder also runs hidden tests so
  students cannot reverse-engineer the test set from the
  solutions.
- Solutions for lab 11 (the capstone).  Lab 11 is open-ended; no
  canonical solution exists.

## Assisted-by

Copilot-CLI:Claude-Opus
