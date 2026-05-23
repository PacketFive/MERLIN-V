#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""
autograder/ci/ci-comment.py — format an autograder JSON manifest as
GitHub-flavoured Markdown for a PR comment or workflow summary.

Reads a "grades manifest" — one JSON object per line, the same format
runner.sh emits — and prints a Markdown table plus a per-lab detail
section.

Usage:
    cat grades.jsonl | ci-comment.py > comment.md

or with a manifest file:

    ci-comment.py grades.jsonl > comment.md

The output is safe to drop into a `peter-evans/create-or-update-comment`
action or into `$GITHUB_STEP_SUMMARY`.
"""

import json
import sys
from pathlib import Path


def grade_letter(score):
    if score >= 90: return "A"
    if score >= 80: return "B"
    if score >= 70: return "C"
    if score >= 60: return "D"
    return "F"


def emoji(score):
    if score == 100: return "🟢"
    if score >= 80:  return "🟢"
    if score >= 60:  return "🟡"
    return "🔴"


def main():
    if len(sys.argv) > 1:
        records = [json.loads(l) for l in Path(sys.argv[1]).read_text().splitlines() if l.strip()]
    else:
        records = [json.loads(l) for l in sys.stdin if l.strip()]

    if not records:
        print("## Autograder")
        print()
        print("> No labs were detected as changed in this push.")
        return

    print("## Autograder Results")
    print()
    print("| Lab | Build | Tests | Score | Grade |")
    print("|-----|-------|-------|-------|-------|")
    total_score = 0
    total_count = 0
    for r in records:
        lab    = r.get("lab", "?")
        build  = r.get("build", "?")
        if build != "ok":
            print(f"| `{lab}` | ❌ fail | — | 0 | F |")
            continue
        passed = r.get("pass", 0)
        total  = r.get("total", 0)
        score  = r.get("score", 0)
        letter = grade_letter(score)
        e      = emoji(score)
        print(f"| `{lab}` | ✅ ok | {passed}/{total} | {e} {score} | **{letter}** |")
        total_score += score
        total_count += 1

    if total_count:
        avg = total_score / total_count
        print()
        print(f"**Aggregate across {total_count} lab(s): {avg:.0f} / 100  ({grade_letter(avg)})**")

    print()
    print("---")
    print()
    print("### Per-lab details")
    print()
    for r in records:
        lab   = r.get("lab", "?")
        build = r.get("build", "?")
        print(f"#### `{lab}`")
        print()
        if build != "ok":
            print(f"- **Build:** ❌ failed")
            print(f"- Inspect `~/.copilot/...` or the workflow log for details.")
            print()
            continue
        passed = r.get("pass", 0)
        total  = r.get("total", 0)
        score  = r.get("score", 0)
        print(f"- **Build:** ✅ ok")
        print(f"- **Tests:** {passed} / {total}")
        print(f"- **Score:** {score} / 100  ({grade_letter(score)})")
        if score < 100:
            print(f"- Re-run locally with `cd docs/academics/labs/{lab}* && make test`"
                  f" to see exactly which tests are failing.")
        print()

    print("---")
    print()
    print("_Autograder run by GitHub Actions / Azure DevOps using the canonical "
          "instructor test files.  Modifying `tests/` or `Makefile` in your "
          "submission has no effect — those files are overlaid before grading._")


if __name__ == "__main__":
    main()
