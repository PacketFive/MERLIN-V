#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""
autograder/score.py — read lab-XX.yml + runner output, print rubric.

Usage:
    runner.sh lab-03 | score.py lab-03

or:

    score.py lab-03 < runner_output.json

Reads a single JSON line of the form produced by runner.sh:

    {"lab":"lab-03","build":"ok","pass":7,"total":7,"score":100}

and prints a rubric breakdown plus a final grade letter.
"""

import json
import sys
import os


def grade_letter(score):
    if score >= 90: return "A"
    if score >= 80: return "B"
    if score >= 70: return "C"
    if score >= 60: return "D"
    return "F"


def main():
    if len(sys.argv) != 2:
        print("usage: score.py <lab-id>", file=sys.stderr)
        sys.exit(2)

    lab = sys.argv[1]

    line = sys.stdin.readline().strip()
    if not line:
        print(f"[score] no input on stdin", file=sys.stderr)
        sys.exit(2)

    try:
        rec = json.loads(line)
    except json.JSONDecodeError as e:
        print(f"[score] invalid JSON: {e}", file=sys.stderr)
        sys.exit(2)

    if rec.get("lab") != lab:
        print(f"[score] lab mismatch: expected {lab}, got {rec.get('lab')}",
              file=sys.stderr)
        sys.exit(2)

    print("=" * 50)
    print(f"  Lab: {rec['lab']}")
    print(f"  Build: {rec.get('build', '?')}")
    if rec.get("build") != "ok":
        print(f"  Tests: skipped (build failed)")
        print(f"  Score: 0  Grade: F")
        print("=" * 50)
        sys.exit(1)

    pass_ = rec.get("pass", 0)
    total = rec.get("total", 0)
    score = rec.get("score", 0)
    print(f"  Tests: {pass_} / {total}")
    print(f"  Score: {score}  Grade: {grade_letter(score)}")
    print("=" * 50)

    sys.exit(0 if score >= 60 else 1)


if __name__ == "__main__":
    main()
