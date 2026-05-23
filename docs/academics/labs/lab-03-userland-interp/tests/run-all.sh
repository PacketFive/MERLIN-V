#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/run-all.sh — run the lab-03 interpreter against every test
# blob and compare to the .expected file.
#
# PROVIDED — do not modify.

set -e
HERE=$(dirname "$0")
BIN="$HERE/../merlin"

fail=0
total=0
for blob in "$HERE"/*.bin; do
	total=$((total + 1))
	name=$(basename "$blob" .bin)
	expected_file="$HERE/$name.expected"
	expected=$(cat "$expected_file")

	# Special: 03-sum-args expects 2 * INITIAL_A0
	if [ "$expected" = '$INITIAL_A0_TIMES_2' ]; then
		actual=$("$BIN" -a 21 "$blob" 2>/dev/null || true)
		expected=42
	else
		actual=$("$BIN" "$blob" 2>/dev/null || true)
	fi

	if [ "$actual" = "$expected" ]; then
		printf "  [PASS] %s  -> %s\n" "$name" "$actual"
	else
		printf "  [FAIL] %s  expected=%s actual=%s\n" \
			"$name" "$expected" "$actual"
		fail=$((fail + 1))
	fi
done

echo
echo "$((total - fail))/$total tests passed."
[ $fail -eq 0 ]
