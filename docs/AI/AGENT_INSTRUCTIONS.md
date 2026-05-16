# AI Agent Instructions for MERLIN-V

> **Project:** MERLIN-V — BPF RISC-V ISA Bytecode JIT VM
> **Upstream tracked:** `net-next` (Linux kernel netdev tree, submodule)
> **Governing policy:** [Linux Kernel AI Policy](https://docs.kernel.org/process/coding-assistants.html)

This document is the authoritative instruction set for every AI coding
assistant (Copilot CLI, Claude, ChatGPT, Cursor, Aider, etc.) operating on
this repository. Human contributors using AI assistance are responsible for
ensuring the agent they drive complies with the rules below.

---

## 1. Scope

These instructions apply to:

- Code, build files, scripts, configuration, and tests in this repository.
- Patches prepared in this repository for submission to the upstream
  `net-next` tree (and any other kernel subsystem trees).
- Documentation under `Documentation/` of the kernel submodule when it is
  being modified through this repository.

They do **not** apply retroactively to the upstream kernel history that
exists in `net-next/`.

---

## 2. Required Reading (in order)

An AI agent MUST be able to apply the conventions in:

1. [Linux Kernel AI Policy / Coding Assistants](https://docs.kernel.org/process/coding-assistants.html)
2. [A guide to the Kernel Development Process](https://docs.kernel.org/process/development-process.html)
3. [Linux kernel coding style](https://docs.kernel.org/process/coding-style.html)
4. [Submitting patches: the essential guide](https://docs.kernel.org/process/submitting-patches.html)
5. [Linux kernel licensing rules](https://docs.kernel.org/process/license-rules.html)
6. Subsystem-specific guides where applicable, e.g. BPF (`Documentation/bpf/`)
   and RISC-V (`Documentation/arch/riscv/`) inside the `net-next` submodule.

If a request conflicts with any of the above, the agent MUST stop and ask
the human operator to clarify or override explicitly.

---

## 3. Legal and Licensing Requirements

- All contributions MUST be compatible with **GPL-2.0-only**.
- New source files MUST include an SPDX license identifier on the first
  line (or second line for shebang scripts), per kernel convention, e.g.:

  ```c
  // SPDX-License-Identifier: GPL-2.0
  ```

  ```sh
  #!/bin/sh
  # SPDX-License-Identifier: GPL-2.0
  ```

- AI agents MUST NOT introduce code, comments, strings, or assets whose
  provenance or license is unknown or incompatible.
- AI agents MUST NOT paste verbatim copyrighted material from training data.

---

## 4. Developer Certificate of Origin (DCO)

Per the Linux Kernel AI Policy:

> AI agents MUST NOT add `Signed-off-by` tags. Only humans can legally
> certify the Developer Certificate of Origin (DCO).

The human submitter is solely responsible for:

- Reviewing all AI-generated code before it leaves their machine.
- Ensuring license compliance.
- Adding their own `Signed-off-by:` trailer.
- Taking full responsibility for the contribution.

An AI agent that produces a commit message MUST omit `Signed-off-by:` lines
entirely and instead leave a clear instruction such as:

```
# Human submitter: add your Signed-off-by: trailer before committing.
```

---

## 5. Attribution (`Assisted-by:` trailer)

This project uses the kernel `Assisted-by:` trailer in its full form, but
restricts `MODEL_VERSION` to the **model family name only** — no version
numbers, no reasoning/context qualifiers, no internal codenames. The
required format is:

```
Assisted-by: AGENT_NAME:MODEL_NAME [TOOL1] [TOOL2]
```

Where:

- `AGENT_NAME` is the AI tool or framework
  (e.g. `Copilot-CLI`, `Cursor`, `Aider`, `Continue`).
- `MODEL_NAME` is the underlying model **family name only**
  (e.g. `Claude-Opus`, `Claude-Sonnet`, `GPT-5`, `Gemini-Pro`,
  `Llama`, `DeepSeek-Coder`).
  - Do NOT include version numbers (`4.7`, `3-opus`, `5.4`).
  - Do NOT include qualifiers (`-high`, `-xhigh`, `-1m-internal`,
    `-codex`, `-mini`, `-preview`).
  - Use a hyphen between words to keep the token whitespace-free so
    tool names parse unambiguously (e.g. `Claude-Opus`, not
    `Claude Opus`).
- `[TOOL1] [TOOL2]` are optional specialized analysis tools
  (e.g. `coccinelle`, `sparse`, `smatch`, `clang-tidy`, `checkpatch`).
- Basic developer tools (`git`, `gcc`, `make`, editors) are NOT listed.

Examples:

```
Assisted-by: Copilot-CLI:Claude-Opus
Assisted-by: Copilot-CLI:Claude-Opus coccinelle sparse
Assisted-by: Cursor:GPT-5 smatch clang-tidy
```

See [`ATTRIBUTION.md`](ATTRIBUTION.md) for additional examples and edge
cases (multiple agents, tool-only assistance, etc.).

---

## 6. Commit and Push Discipline

AI agents MUST NOT execute:

- `git commit`, `git commit --amend`
- `git push`, `git push --force`
- `git tag`, `git rebase`, `git reset --hard`
- `git format-patch ... | git send-email`
- Any other operation that mutates repository history or publishes work.

Instead, the agent MUST:

1. Stage changes in the working tree (or simply leave them unstaged).
2. Print:
   - A complete commit message draft (subject + body + `Assisted-by:`),
     omitting `Signed-off-by:`.
   - The exact `git` command(s) the human should run.
3. Wait for the human to commit manually.

---

## 7. Coding Standards

When generating or modifying C code targeting the kernel or kernel-adjacent
components:

- Follow `Documentation/process/coding-style.rst` (tabs, 8-column indent,
  brace placement, naming, etc.).
- Run/recommend `scripts/checkpatch.pl --strict` on every patch.
- Prefer kernel idioms (`kmalloc`/`kfree`, `container_of`, `READ_ONCE`,
  `WRITE_ONCE`, RCU primitives, `likely`/`unlikely`) over generic C.
- Never introduce floating-point in kernel code paths.
- For BPF JIT work, respect the BPF ABI and verifier expectations.
- For RISC-V work, respect the targeted ISA extensions and existing
  encoding helpers in `arch/riscv/`.

When generating Rust code (kernel Rust enabled via `.rustfmt.toml` /
`.clippy.toml`):

- Follow the kernel Rust subsystem conventions.
- Run `rustfmt` with the in-tree configuration.
- Heed `clippy` lints configured by the project.

---

## 8. Required Pre-Handoff Checks

Before declaring a change complete, the AI agent SHOULD have verified (or
explicitly noted as not run, with reason) at least the following:

- [ ] `scripts/checkpatch.pl --strict` clean on each patch.
- [ ] Build succeeds for the affected configuration
      (e.g. `make ARCH=riscv defconfig && make ARCH=riscv -j$(nproc)`).
- [ ] Relevant selftests pass (`tools/testing/selftests/bpf/`,
      `tools/testing/selftests/riscv/`).
- [ ] Static analyzers run when reasonable: `sparse`, `smatch`,
      `coccinelle` semantic patches.
- [ ] No new compiler warnings (`W=1` where practical).
- [ ] SPDX identifiers on any new files.
- [ ] Commit messages follow `Documentation/process/submitting-patches.rst`
      (imperative subject ≤ ~72 chars, prefix like `bpf, riscv:` where
      appropriate, body wrapped at 72 columns).

When a check is skipped, the agent MUST say so in its handoff so the human
can decide whether to run it.

---

## 9. Safety, Honesty, and Uncertainty

- Surface assumptions explicitly. Do not silently invent APIs, register
  names, instruction encodings, syscall numbers, or struct layouts.
- If a fact cannot be verified from the repository or cited documentation,
  say so and ask.
- Prefer small, reviewable patches over large rewrites.
- Do not exfiltrate code, credentials, or kernel debug data to third-party
  services beyond what the operator has explicitly authorized.

---

## 10. Updating This Policy

Changes to this document or anything else under `docs/AI/` should themselves
follow the rules above (no `Signed-off-by:` from the agent, proper
`Assisted-by:` trailer, human commits). Material changes should reference
any upstream kernel policy revision that motivated them.
