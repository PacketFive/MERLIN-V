#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-or-later
#
# autograder/ci/instructor-overlay.sh — overlay canonical instructor
# files on top of a student tree before grading.
#
# Usage:
#   instructor-overlay.sh <lab-id> <instructor-tree-root>
#
# For the given lab, replaces:
#   - the Makefile
#   - the tests/ directory
#   - the src/*.h headers that were marked PROVIDED
#   - the autograder/lab-NN.yml manifest (for sanity)
#
# with the versions from <instructor-tree-root>.  Student-modifiable
# files (src/*.c with TODO blocks, the README, exercises.c for lab-02)
# are left alone.
#
# This is the integrity gate: a student cannot pass the autograder by
# deleting the tests or rewriting the Makefile to run nothing.

set -eu

LAB="${1:-}"
INSTRUCTOR="${2:-}"

if [ -z "$LAB" ] || [ -z "$INSTRUCTOR" ]; then
	echo "usage: $0 <lab-id> <instructor-tree-root>" >&2
	echo "  e.g. $0 lab-03 /tmp/instructor-checkout" >&2
	exit 2
fi

HERE=$(cd "$(dirname "$0")" && pwd)
LABS_DIR=$(cd "$HERE/../.." && pwd)
STUDENT_LAB=$(ls -d "$LABS_DIR/$LAB"* 2>/dev/null | head -1)
# Resolve instructor path to absolute so we can compare reliably.
INSTRUCTOR_ABS=$(cd "$INSTRUCTOR" 2>/dev/null && pwd || echo "$INSTRUCTOR")
INSTRUCTOR_LAB=$(ls -d "$INSTRUCTOR_ABS/docs/academics/labs/$LAB"* 2>/dev/null | head -1)

if [ ! -d "$STUDENT_LAB" ]; then
	echo "[overlay] student lab not found: $LAB under $LABS_DIR" >&2
	exit 2
fi
if [ ! -d "$INSTRUCTOR_LAB" ]; then
	echo "[overlay] instructor lab not found: $LAB under $INSTRUCTOR" >&2
	exit 2
fi

if [ "$STUDENT_LAB" = "$INSTRUCTOR_LAB" ]; then
	echo "[overlay] $LAB: student and instructor point at the same path; nothing to do." >&2
	exit 0
fi

echo "[overlay] $LAB:" >&2
echo "  student   = $STUDENT_LAB" >&2
echo "  instructor= $INSTRUCTOR_LAB" >&2

# Always overlay Makefile + tests/ + the autograder yml.
cp -f "$INSTRUCTOR_LAB/Makefile" "$STUDENT_LAB/Makefile"
echo "  + Makefile" >&2

if [ -d "$INSTRUCTOR_LAB/tests" ]; then
	rm -rf "$STUDENT_LAB/tests"
	cp -a "$INSTRUCTOR_LAB/tests" "$STUDENT_LAB/tests"
	echo "  + tests/" >&2
fi

# Overlay PROVIDED headers (interp.h, verify.h, decode.c).
# We detect "PROVIDED" by grepping the instructor copy's leading
# comment header.
for f in src/interp.h src/verify.h src/decode.c src/main.c; do
	if [ -f "$INSTRUCTOR_LAB/$f" ] && \
	   head -10 "$INSTRUCTOR_LAB/$f" | grep -q 'PROVIDED'; then
		mkdir -p "$STUDENT_LAB/src"
		cp -f "$INSTRUCTOR_LAB/$f" "$STUDENT_LAB/$f"
		echo "  + $f" >&2
	fi
done

# Common headers (always overlay)
if [ -d "$INSTRUCTOR/docs/academics/labs/common" ]; then
	rm -rf "$LABS_DIR/common"
	cp -a "$INSTRUCTOR/docs/academics/labs/common" "$LABS_DIR/common"
	echo "  + common/ (shared)" >&2
fi

# Autograder manifest (always overlay)
INST_YML="$INSTRUCTOR/docs/academics/labs/autograder/${LAB}.yml"
if [ -f "$INST_YML" ]; then
	mkdir -p "$LABS_DIR/autograder"
	cp -f "$INST_YML" "$LABS_DIR/autograder/${LAB}.yml"
	echo "  + autograder/${LAB}.yml" >&2
fi

echo "[overlay] $LAB: instructor files in place." >&2
