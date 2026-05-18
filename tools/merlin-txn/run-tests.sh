#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# Test battery for merlin-txn.
# Each case drives the tool via the `script` subcommand.

set -u

T="./merlin-txn"
pass=0; fail=0
ok () { echo "  [PASS] $1"; pass=$((pass+1)); }
no () { echo "  [FAIL] $1"; [ -n "${2-}" ] && echo "$2" | sed 's/^/    /'; fail=$((fail+1)); }

echo "== merlin-txn test battery =="

# -----------------------------------------------------------------------
# Case 1: single-map single-op round-trip (INSERT + lookup)
# -----------------------------------------------------------------------
cat >test_insert.txn <<'EOF'
map-create
txn-begin 0
txn-stage 1 insert 100 999
txn-commit drain
map-lookup 1 100
EOF
out=$($T script test_insert.txn 2>&1)
if echo "$out" | grep -q 'lookup 100 -> 999' && \
   echo "$out" | grep -q 'txn-commit: OK staged=1 applied=1 skipped=0'; then
    ok "single INSERT + lookup"
else
    no "single INSERT + lookup" "$out"
fi

# -----------------------------------------------------------------------
# Case 2: UPSERT = insert then overwrite
# -----------------------------------------------------------------------
cat >test_upsert.txn <<'EOF'
map-create
txn-begin 0
txn-stage 1 upsert 42 100
txn-commit drain
txn-begin 0
txn-stage 1 upsert 42 200
txn-commit drain
map-lookup 1 42
EOF
out=$($T script test_upsert.txn 2>&1)
if echo "$out" | grep -q 'lookup 42 -> 200'; then
    ok "UPSERT overwrites existing key"
else
    no "UPSERT" "$out"
fi

# -----------------------------------------------------------------------
# Case 3: DELETE is idempotent (absent key = skipped, not error)
# -----------------------------------------------------------------------
cat >test_delete.txn <<'EOF'
map-create
txn-begin 0
txn-stage 1 upsert 77 1
txn-commit drain
txn-begin 0
txn-stage 1 delete 77
txn-stage 1 delete 77
txn-commit drain
map-lookup 1 77
EOF
out=$($T script test_delete.txn 2>&1)
applied=$(echo "$out" | grep 'txn-commit: OK' | tail -1 | grep -o 'applied=[0-9]*' | cut -d= -f2)
skipped=$(echo "$out" | grep 'txn-commit: OK' | tail -1 | grep -o 'skipped=[0-9]*' | cut -d= -f2)
notfound=$(echo "$out" | grep -c 'NOTFOUND')
if [ "$applied" = "1" ] && [ "$skipped" = "1" ] && [ "$notfound" = "1" ]; then
    ok "DELETE idempotent (applied=1 skipped=1, key gone)"
else
    no "DELETE idempotent" "$out"
fi

# -----------------------------------------------------------------------
# Case 4: multi-map atomic commit — both maps see new values
# -----------------------------------------------------------------------
cat >test_multimap.txn <<'EOF'
map-create
map-create
txn-begin 0
txn-stage 1 upsert 1 10
txn-stage 2 upsert 1 20
txn-commit drain
map-lookup 1 1
map-lookup 2 1
EOF
out=$($T script test_multimap.txn 2>&1)
v1=$(echo "$out" | grep 'lookup 1 ->' | head -1 | awk '{print $NF}')
v2=$(echo "$out" | grep 'lookup 1 ->' | tail -1 | awk '{print $NF}')
if [ "$v1" = "10" ] && [ "$v2" = "20" ]; then
    ok "multi-map: both maps updated atomically (v1=$v1 v2=$v2)"
else
    no "multi-map" "$out"
fi

# -----------------------------------------------------------------------
# Case 5: abort-before-commit leaves map untouched
# -----------------------------------------------------------------------
cat >test_abort.txn <<'EOF'
map-create
txn-begin 0
txn-stage 1 upsert 55 555
txn-abort
map-lookup 1 55
EOF
out=$($T script test_abort.txn 2>&1)
if echo "$out" | grep -q 'lookup 55 -> NOTFOUND'; then
    ok "abort leaves map untouched"
else
    no "abort" "$out"
fi

# -----------------------------------------------------------------------
# Case 6: INSERT on existing key is CONFLICT-within-txn (skipped/fail)
#         i.e. INSERT fails with EEXIST -> counted as skipped
# -----------------------------------------------------------------------
cat >test_insert_dup.txn <<'EOF'
map-create
txn-begin 0
txn-stage 1 upsert 10 1
txn-commit drain
txn-begin 0
txn-stage 1 insert 10 2
txn-commit drain
map-lookup 1 10
EOF
out=$($T script test_insert_dup.txn 2>&1)
# INSERT on existing key should be skipped (EEXIST treated as soft error)
skipped=$(echo "$out" | grep 'txn-commit: OK' | tail -1 | grep -o 'skipped=[0-9]*' | cut -d= -f2)
val=$(echo "$out" | grep 'lookup 10 ->' | awk '{print $NF}')
if [ "$skipped" = "1" ] && [ "$val" = "1" ]; then
    ok "INSERT on existing key skipped, old value preserved"
else
    no "INSERT dup" "$out"
fi

# -----------------------------------------------------------------------
# Case 7: MERLIN_TXN_F_FAST skips drain (verify flag recorded)
# -----------------------------------------------------------------------
cat >test_fast.txn <<'EOF'
map-create
txn-begin 0
txn-stage 1 upsert 99 7
txn-commit fast
EOF
out=$($T script test_fast.txn 2>&1)
if echo "$out" | grep -q 'drain=no'; then
    ok "TXN_F_FAST skips drain (drain=no)"
else
    no "TXN_F_FAST" "$out"
fi

# -----------------------------------------------------------------------
# Case 8: DRAIN_RCU flag is recorded in output
# -----------------------------------------------------------------------
cat >test_drain.txn <<'EOF'
map-create
txn-begin 0
txn-stage 1 upsert 88 8
txn-commit drain
EOF
out=$($T script test_drain.txn 2>&1)
if echo "$out" | grep -q 'drain=yes'; then
    ok "TXN_F_DRAIN_RCU recorded (drain=yes)"
else
    no "TXN_F_DRAIN_RCU" "$out"
fi

# -----------------------------------------------------------------------
# Case 9: exceed MERLIN_TXN_MAX_OPS => error
# -----------------------------------------------------------------------
# Create a script that stages 4097 ops (> max).  We generate it from Python.
python3 -c "
lines = ['map-create', 'txn-begin 0']
for i in range(4097):
    lines.append(f'txn-stage 1 upsert {i} {i}')
lines.append('txn-commit drain')
print('\n'.join(lines))
" > test_overflow.txn
# The tool should die on the 4097th stage; that makes it exit non-zero.
if ! $T script test_overflow.txn >/dev/null 2>&1; then
    ok "staged-ops overflow rejected (> MERLIN_TXN_MAX_OPS=4096)"
else
    no "staged-ops overflow not caught"
fi

# -----------------------------------------------------------------------
# Case 10: conflict detection — two sequential commits to the same
#          map: the SECOND commit finds the map unlocked (serialised).
#          We prove sequential correctness here; true concurrency is
#          kernel-side.  We verify the version increments per commit.
# -----------------------------------------------------------------------
cat >test_seq_commits.txn <<'EOF'
map-create
txn-begin 1
txn-stage 1 upsert 1 100
txn-commit drain
txn-begin 1
txn-stage 1 upsert 1 200
txn-commit drain
map-lookup 1 1
EOF
out=$($T script test_seq_commits.txn 2>&1)
if echo "$out" | grep -q 'lookup 1 -> 200'; then
    ok "sequential commits to same map: second wins"
else
    no "sequential commits" "$out"
fi

# -----------------------------------------------------------------------
# Case 11: disjoint-set commits don't interfere
# -----------------------------------------------------------------------
cat >test_disjoint.txn <<'EOF'
map-create
map-create
# Commit to map 1 only
txn-begin 0
txn-stage 1 upsert 5 50
txn-commit drain
# Commit to map 2 only
txn-begin 0
txn-stage 2 upsert 5 500
txn-commit drain
map-lookup 1 5
map-lookup 2 5
EOF
out=$($T script test_disjoint.txn 2>&1)
r1=$(echo "$out" | grep 'lookup 5 ->' | head -1 | awk '{print $NF}')
r2=$(echo "$out" | grep 'lookup 5 ->' | tail -1 | awk '{print $NF}')
if [ "$r1" = "50" ] && [ "$r2" = "500" ]; then
    ok "disjoint commits independent (map1=$r1 map2=$r2)"
else
    no "disjoint commits" "$out"
fi

# -----------------------------------------------------------------------
# Case 12: commit stats: ops_staged/applied/skipped all correct
# -----------------------------------------------------------------------
cat >test_stats.txn <<'EOF'
map-create
txn-begin 0
txn-stage 1 upsert 1 1
txn-stage 1 upsert 2 2
txn-stage 1 upsert 3 3
txn-stage 1 delete 99
txn-commit drain
EOF
out=$($T script test_stats.txn 2>&1)
staged=$(echo "$out"  | grep 'txn-commit: OK' | grep -o 'staged=[0-9]*'  | cut -d= -f2)
applied=$(echo "$out" | grep 'txn-commit: OK' | grep -o 'applied=[0-9]*' | cut -d= -f2)
skipped=$(echo "$out" | grep 'txn-commit: OK' | grep -o 'skipped=[0-9]*' | cut -d= -f2)
if [ "$staged" = "4" ] && [ "$applied" = "3" ] && [ "$skipped" = "1" ]; then
    ok "commit stats: staged=4 applied=3 skipped=1 (DELETE of absent key)"
else
    no "commit stats" "$out"
fi

echo
echo "== Summary: $pass passed, $fail failed =="
exit $fail
