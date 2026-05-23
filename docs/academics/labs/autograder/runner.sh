#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-or-later
#
# autograder/runner.sh — one-shot grader for any lab.
#
# Usage:  runner.sh lab-02 | lab-03 | lab-04
#
# Builds the student's tree, runs the per-lab test cases, and emits
# a JSON-line score report to stdout plus a human-readable summary
# to stderr.

set -e

LAB="$1"
if [ -z "$LAB" ]; then
	echo "usage: $0 <lab-id>"
	echo "       lab-id ∈ {lab-02, lab-03, lab-04}"
	exit 2
fi

HERE=$(cd "$(dirname "$0")" && pwd)
LABS_DIR=$(cd "$HERE/.." && pwd)

case "$LAB" in
lab-02)
	DIR="$LABS_DIR/lab-02-riscv-isa-primer"
	;;
lab-03)
	DIR="$LABS_DIR/lab-03-userland-interp"
	;;
lab-04)
	DIR="$LABS_DIR/lab-04-minimal-verifier"
	;;
*)
	echo "unknown lab: $LAB" >&2
	exit 2
	;;
esac

if [ ! -d "$DIR" ]; then
	echo "lab directory not found: $DIR" >&2
	exit 2
fi

cd "$DIR"

echo "[grader] cleaning + building $LAB ..." >&2
make clean >/dev/null 2>&1 || true
if ! make >/tmp/grader-$LAB.build.log 2>&1; then
	echo "[grader] BUILD FAILED — see /tmp/grader-$LAB.build.log" >&2
	cat /tmp/grader-$LAB.build.log >&2
	printf '{"lab":"%s","build":"fail","score":0}\n' "$LAB"
	exit 1
fi
echo "[grader] build OK." >&2

echo "[grader] running tests ..." >&2
total=0
pass=0
if make test >/tmp/grader-$LAB.test.log 2>&1; then
	# Extract "N/M tests passed."
	line=$(grep -oE '[0-9]+/[0-9]+ tests passed' /tmp/grader-$LAB.test.log | tail -1)
	pass=${line%%/*}
	total=${line##*/}
	total=${total% tests passed}
else
	line=$(grep -oE '[0-9]+/[0-9]+ tests passed' /tmp/grader-$LAB.test.log | tail -1)
	if [ -n "$line" ]; then
		pass=${line%%/*}
		total=${line##*/}
		total=${total% tests passed}
	fi
fi

if [ -z "$total" ] || [ "$total" -eq 0 ]; then
	echo "[grader] could not parse test results; see /tmp/grader-$LAB.test.log" >&2
	score=0
else
	score=$(awk -v p="$pass" -v t="$total" 'BEGIN{printf "%.0f", p*100/t}')
fi

echo "[grader] $LAB: $pass/$total tests, score=$score" >&2

# Emit JSON
printf '{"lab":"%s","build":"ok","pass":%s,"total":%s,"score":%s}\n' \
	"$LAB" "$pass" "$total" "$score"

# Exit non-zero if score < 60 (instructor-tunable)
[ "$score" -ge 60 ]
