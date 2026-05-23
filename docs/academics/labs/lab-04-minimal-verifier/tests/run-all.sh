#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-or-later
# PROVIDED — do not modify.

set -e
HERE=$(dirname "$0")
BIN="$HERE/../merlin-verify"

fail=0
total=0

for blob in "$HERE"/accept/*.bin; do
	total=$((total + 1))
	name=$(basename "$blob")
	if "$BIN" "$blob" >/dev/null 2>&1; then
		printf "  [PASS] accept/%s\n" "$name"
	else
		printf "  [FAIL] accept/%s  (verifier rejected — should have accepted)\n" \
			"$name"
		fail=$((fail + 1))
	fi
done

for blob in "$HERE"/reject/*.bin; do
	total=$((total + 1))
	name=$(basename "$blob")
	if "$BIN" "$blob" >/dev/null 2>&1; then
		printf "  [FAIL] reject/%s  (verifier accepted — should have rejected)\n" \
			"$name"
		fail=$((fail + 1))
	else
		printf "  [PASS] reject/%s\n" "$name"
	fi
done

echo
echo "$((total - fail))/$total tests passed."
[ $fail -eq 0 ]
