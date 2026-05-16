# AI Agent Documentation

This directory contains instructions, policies, and conventions for AI coding
assistants (and their human operators) contributing to **MERLIN-V**
(BPF RISC-V ISA Bytecode JIT VM).

MERLIN-V tracks and contributes to the upstream Linux kernel `net-next` tree
(see `.gitmodules`), so all AI-assisted work in this repository follows the
[Linux Kernel AI Policy](https://docs.kernel.org/process/coding-assistants.html)
with the project-specific adjustments documented here.

## Contents

| File | Purpose |
| ---- | ------- |
| [`AGENT_INSTRUCTIONS.md`](AGENT_INSTRUCTIONS.md) | Authoritative instructions for any AI agent operating in this repo |
| [`ATTRIBUTION.md`](ATTRIBUTION.md) | Required `Assisted-by:` trailer format and examples |

## TL;DR for AI Agents

1. Follow Linux kernel process docs: development process, coding style,
   submitting patches, and license rules.
2. **Never** add `Signed-off-by:` trailers. Only the human submitter may
   certify the Developer Certificate of Origin (DCO).
3. **Never** run `git commit`, `git push`, `git tag`, or any history-mutating
   operation. Produce a commit message and the exact `git` commands; the
   human operator will execute them.
4. Use the project attribution format. It follows the kernel template but
   uses the **model name only** — no version numbers, no qualifiers:

   ```
   Assisted-by: AGENT_NAME:MODEL_NAME [TOOL1] [TOOL2]
   ```

   Example: `Assisted-by: Copilot-CLI:Claude-Opus coccinelle sparse`

5. Keep all contributions GPL-2.0-only compatible and use proper SPDX
   identifiers on new files.
