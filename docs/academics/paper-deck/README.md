# Course-Companion Paper + Slide Deck

This directory contains the short course-companion materials that
accompany the MERLIN-V advanced-OS course in `docs/academics/`.

## Layout

```
docs/academics/paper-deck/
├── README.md          this file
├── paper.tex          short workshop / educational-track paper
├── slides.tex         Beamer slide deck for a guest lecture or talk
├── refs.bib           shared bibliography
└── Makefile           latexmk + bibtex build
```

## What this is for

Two artefacts:

1. **`paper.tex`** — a 6-page educational-track paper that argues for
   teaching kernel-extension VMs as a *vehicle* for teaching modern
   operating-systems design (verifier soundness, JIT codegen, cross-OS
   portability, sandboxing without an MMU). Submittable to venues like
   USENIX SIGCSE, the OS Educator workshops at SOSP/OSDI, ACM
   SIGOPS, or the Linux Plumbers OS-track education BoF.

2. **`slides.tex`** — Beamer slides for a 45-minute guest lecture or
   conference talk version of the same argument.

The paper and slides share a bibliography (`refs.bib`) and a
worked example: the very same `pkt[12] == 0x08 ? PASS : DROP`
classifier that runs on the ESP32-C3 and the MPFS Icicle Kit in
`platforms/*/sample-classifier/`. The point is end-to-end: the
slides show students a 7-instruction RV32 program that the
course's labs build the compiler / verifier / JIT / runtime to
execute, on real hardware.

## Build

```bash
make            # paper.pdf + slides.pdf
make paper      # paper.pdf only
make slides     # slides.pdf only
make clean
```

Dependencies: `latexmk`, `pdflatex`, `biber` (or `bibtex` —
`latexmk` will pick one).

## License

- `paper.tex`, `slides.tex`: CC-BY-SA-4.0 (text)
- Code excerpts inside both: GPL-2.0-only / Apache-2.0 as per
  the parent project.

## Assisted-by

Copilot-CLI:Claude-Opus
