# Lab Code Skeletons + Autograders

This directory contains the **student-facing starter code** and the
**instructor-side autograders** for the labs in `docs/academics/lab-*.md`.

> **Status.** Initial skeleton drop. The course prose (lab statements,
> learning objectives, grading rubric prose) lives one directory up at
> `docs/academics/lab-*.md`. This directory is the *code* counterpart.

## Layout

```
docs/academics/labs/
├── README.md                  this file
├── common/
│   ├── elf.h                  small Elf32 reader (used by labs 03+)
│   ├── elf.c
│   ├── rv32.h                 RV32 register names and opcode constants
│   └── test_helpers.h         assert macros for student tests
├── lab-02-riscv-isa-primer/
│   └── README.md              pointers + warm-up exercises (no big code)
├── lab-03-userland-interp/
│   ├── README.md
│   ├── Makefile
│   ├── src/
│   │   ├── interp.h           public interface (PROVIDED)
│   │   ├── interp.c           PARTIALLY PROVIDED — students fill TODO blocks
│   │   ├── decode.c           PROVIDED — decoder reference impl
│   │   └── main.c             PROVIDED — CLI driver
│   └── tests/
│       ├── 01-addi.s          tiny .S to assemble offline
│       ├── 01-addi.bin        pre-assembled blob (no toolchain needed)
│       ├── 02-loop-bounded.bin
│       └── ...
├── lab-04-minimal-verifier/
│   ├── README.md
│   ├── Makefile
│   ├── src/
│   │   ├── verify.h           public interface (PROVIDED)
│   │   ├── verify.c           SKELETON — students implement abstract domain
│   │   └── main.c             PROVIDED — CLI driver
│   └── tests/
│       ├── accept/            programs the verifier must accept
│       └── reject/            programs the verifier must reject
├── lab-05-objtool/, lab-06-passthrough/, lab-07-host-jit/  (skeleton stubs)
└── autograder/
    ├── runner.sh              one-shot grader for any lab
    ├── lab-03.yml             expected outputs per test case
    ├── lab-04.yml
    └── score.py               read lab-XX.yml + runner output, print rubric
```

## Provided vs. student-written

Each file's header notes one of:

- `/* PROVIDED — do not modify */`
- `/* PARTIALLY PROVIDED — fill in TODO blocks */`
- `/* SKELETON — students implement */`

Autograder runs against the *student's* tree and never modifies the
provided files. If a student wins by deleting the test cases, the
autograder catches it (`runner.sh` re-derives them from `autograder/`).

## How to build / run any lab

```bash
cd docs/academics/labs/lab-03-userland-interp
make                        # builds the interpreter
make test                   # runs the lab's own test suite
make grade                  # runs autograder/runner.sh against this lab
```

## How the autograder works

```bash
docs/academics/labs/autograder/runner.sh lab-03
```

It:

1. Builds the student's `lab-03-userland-interp` tree.
2. For each entry in `autograder/lab-03.yml`, runs the student binary
   against an input from `tests/` and compares stdout + exit code.
3. Pipes the JSON test results through `autograder/score.py` to
   produce a rubric score (0-100) and a per-test breakdown.

Both the test inputs and the expected outputs are committed; students
can rerun the autograder locally and see exactly what's failing
before submitting.

## Solutions: instructor access

Reference solutions live on a **separate private repository**
(`PacketFive/MERLIN-V-solutions`), not on a branch of this repo.
The public repo (this one) ships the lab skeletons + autograder
+ CI pipelines; the private repo mirrors the same tree with the
TODO blocks filled in.

Instructors at accredited institutions can request access via
`academics@packetfive.com`.  The workflow, reference-implementation
crib, and instructor-agreement template are all documented in
[`solutions-workflow/`](solutions-workflow/).

Students see this directory; instructors see this directory *plus*
the private solutions repo.

## CI / autograder pipeline

This autograder also runs in **GitHub Actions** and **Azure DevOps**
pipelines so students get graded automatically on every push. The
pipeline checks out a canonical instructor tree, overlays the
instructor's `tests/` and `Makefile` on top of the student tree (so
students can't game the test suite), and posts a Markdown grade
summary as a PR comment.

See [`CI.md`](CI.md) for the full pipeline documentation, setup
instructions for student forks, and the instructor workflow for
rolling out fixes mid-semester.

## License

The lab skeletons in this directory are released under the same
license as the parent project. The reference solutions on the
private `PacketFive/MERLIN-V-solutions` repo are released to
instructors only on request; redistributing solutions is a
violation of the academic honesty policy (see
[`solutions-workflow/INSTRUCTOR-AGREEMENT.md`](solutions-workflow/INSTRUCTOR-AGREEMENT.md)).

## Assisted-by

Copilot-CLI:Claude-Opus
