#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-or-later
#
# autograder/ci/changed-labs.sh — given a git ref to compare against,
# print one-per-line the lab IDs that have changes in the working tree.
#
# Usage:
#   changed-labs.sh [base-ref]
#
# Default base-ref is `origin/main`.  Output is a sorted, deduplicated
# list of lab IDs (e.g. "lab-02", "lab-03").  Empty stdout means no
# labs changed and the autograder should not run.

set -e

BASE="${1:-origin/main}"
HERE=$(cd "$(dirname "$0")" && pwd)
LABS_DIR=$(cd "$HERE/../.." && pwd)

# Get list of changed files under docs/academics/labs/lab-*/
# relative to BASE.
if ! git rev-parse --verify "$BASE" >/dev/null 2>&1; then
	# No base ref (first push to a fork) — grade every lab.
	cd "$LABS_DIR"
	for d in lab-*; do
		[ -d "$d" ] || continue
		# Extract the "lab-NN" prefix (drop the descriptive suffix)
		echo "$d" | sed -E 's/^(lab-[0-9]+).*$/\1/'
	done | sort -u
	exit 0
fi

# Normal mode: compare working tree against base.
cd "$LABS_DIR/.."  # go to docs/academics/

git diff --name-only "$BASE" -- labs/ | \
	awk -F/ '/^labs\/lab-/ { print $2 }' | \
	sed -E 's/^(lab-[0-9]+).*$/\1/' | \
	sort -u
