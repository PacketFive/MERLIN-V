# Attribution Format

This project follows the Linux Kernel
[AI Policy attribution](https://docs.kernel.org/process/coding-assistants.html#attribution)
with **one project-specific deviation**: the `MODEL_VERSION` slot carries
the **model family name only** — no version numbers, no qualifiers, no
internal codenames.

## Canonical Format

```
Assisted-by: AGENT_NAME:MODEL_NAME [TOOL1] [TOOL2]
```

- `AGENT_NAME` — the AI tool or framework name
  (e.g. `Copilot-CLI`, `Cursor`, `Aider`).
- `MODEL_NAME` — the underlying model **family name only**
  (e.g. `Claude-Opus`, `Claude-Sonnet`, `GPT-5`, `Gemini-Pro`).
  - ❌ No version numbers (`4.7`, `3-opus`, `5.4`).
  - ❌ No qualifiers (`-high`, `-xhigh`, `-1m-internal`, `-codex`,
    `-mini`, `-preview`).
  - ✅ Use a hyphen between words so the token has no whitespace
    (`Claude-Opus`, not `Claude Opus`).
- `[TOOL1] [TOOL2] ...` — optional specialized analysis tools used.
- Basic development tools (`git`, `gcc`, `make`, `clang`, editors) are
  **not** listed.

## Placement

The `Assisted-by:` trailer goes in the **trailer block** of the commit
message, alongside other trailers (e.g. `Reviewed-by:`, `Tested-by:`,
`Reported-by:`, and — added by the human submitter — `Signed-off-by:`).

Example commit message:

```
bpf, riscv: optimize call-site patching for tail calls

<body paragraphs wrapped at 72 columns>

Reported-by: Jane Reviewer <jane@example.org>
Assisted-by: Copilot-CLI:Claude-Opus coccinelle sparse
Signed-off-by: Your Name <you@example.org>   <-- added by human, not AI
```

## Examples

Agent + model, no specialized tools:

```
Assisted-by: Copilot-CLI:Claude-Opus
```

Agent + model with semantic patch and static analyzer assistance:

```
Assisted-by: Copilot-CLI:Claude-Opus coccinelle sparse
```

Multiple agents/models on the same change (one trailer per pairing):

```
Assisted-by: Copilot-CLI:Claude-Opus smatch
Assisted-by: Cursor:GPT-5 clang-tidy
```

Specialized tools only, used directly by the human without an AI agent
generating code: **do not** add an `Assisted-by:` trailer — that trailer is
reserved for AI-assisted contributions. Mention the tools in the commit
body if relevant.

## What NOT to Do

- ❌ Do not include a model version or qualifier:
  - `Assisted-by: Claude:claude-3-opus`
  - `Assisted-by: Copilot-CLI:Claude-Opus-4.7`
  - `Assisted-by: Copilot-CLI:Claude-Opus-xhigh`
  - `Assisted-by: Copilot-CLI:GPT-5.3-codex`
- ❌ Do not use whitespace inside `MODEL_NAME`:
  - `Assisted-by: Copilot-CLI:Claude Opus`  *(parses as model "Claude"
    + tool "Opus")*
  - Use `Claude-Opus` instead.
- ❌ Do not omit the model when one was used:
  `Assisted-by: Copilot-CLI` *(must be `Copilot-CLI:MODEL_NAME`)*.
- ❌ Do not list generic tools:
  `Assisted-by: Copilot-CLI:Claude-Opus git make vim`.
- ❌ Do not put the trailer in the subject line or body paragraphs.
- ❌ Do not add `Signed-off-by:` as the AI agent — that is the human
  submitter's responsibility (see `AGENT_INSTRUCTIONS.md` §4).
