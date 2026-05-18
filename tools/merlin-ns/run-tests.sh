#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# Test battery for merlin-ns.

set -u

N="./merlin-ns"
pass=0; fail=0
ok () { echo "  [PASS] $1"; pass=$((pass+1)); }
no () { echo "  [FAIL] $1"; [ -n "${2-}" ] && echo "$2" | sed 's/^/    /'; fail=$((fail+1)); }

# MERLIN_ATTACH_MVDP = 1 (per uapi/linux/merlin.h)
# MVDP helper IDs: 0x201 mvdp_redirect, 0x110 ktime_get_ns,
#                  0x101 map_lookup_elem.

rm -f test_*.ns

echo "== merlin-ns test battery =="

# --- create a permissive parent ---
$N create --name parent \
    --attach 1 \
    --helpers 0x101,0x110,0x201 \
    --max-progs 10 --max-maps 10 \
    --max-map-mem 1048576 \
    -o test_parent.ns >/dev/null
[ -f test_parent.ns ] && ok "create parent" || no "create parent"

# --- dump shows expected sets ---
DUMP=$($N dump test_parent.ns)
if echo "$DUMP" | grep -q 'permit_attach (1)' && \
   echo "$DUMP" | grep -q 'permit_helper (3)'; then
    ok "dump reports counts (attach=1, helper=3)"
else
    no "dump output" "$DUMP"
fi

# --- check ACCEPT: program uses permitted attach + helpers ---
if $N check --ns test_parent.ns --attach 1 --helpers 0x110,0x201 >/tmp/c 2>&1; then
    ok "check accepts permitted program"
else
    no "check should accept" "$(cat /tmp/c)"
fi

# --- check REJECT: unknown helper ---
if $N check --ns test_parent.ns --attach 1 --helpers 0x303 >/tmp/c 2>&1; then
    no "check should reject disallowed helper"
else
    if grep -q 'helper_not_allowed' /tmp/c && grep -q '0x303' /tmp/c; then
        ok "check rejects disallowed helper (0x303)"
    else
        no "rejection reason wrong" "$(cat /tmp/c)"
    fi
fi

# --- check REJECT: wrong attach type ---
if $N check --ns test_parent.ns --attach 5 --helpers 0x110 >/tmp/c 2>&1; then
    no "check should reject wrong attach"
else
    if grep -q 'attach_not_allowed' /tmp/c; then
        ok "check rejects wrong attach (type 5)"
    else
        no "rejection reason wrong" "$(cat /tmp/c)"
    fi
fi

# --- check REJECT: quota (prog count) ---
if $N check --ns test_parent.ns --attach 1 --helpers 0x110 \
            --usage progs:10 >/tmp/c 2>&1; then
    no "check should reject when at prog quota"
else
    if grep -q 'quota_progs' /tmp/c; then
        ok "check rejects when at prog quota"
    else
        no "rejection reason wrong" "$(cat /tmp/c)"
    fi
fi

# --- check REJECT: quota (map mem) ---
if $N check --ns test_parent.ns --attach 1 --helpers 0x110 \
            --map-mem 2000000 >/tmp/c 2>&1; then
    no "check should reject when over map_mem quota"
else
    if grep -q 'quota_map_mem' /tmp/c; then
        ok "check rejects over map_mem quota"
    else
        no "rejection reason wrong" "$(cat /tmp/c)"
    fi
fi

# --- compose: child subset is accepted ---
$N create --name child_ok \
    --attach 1 \
    --helpers 0x110 \
    --max-progs 5 \
    -o test_child_ok.ns >/dev/null
if $N compose --child test_child_ok.ns --parent test_parent.ns \
              -o test_eff.ns >/tmp/cmp 2>&1; then
    ok "compose accepts child subset"
else
    no "compose rejected legal child" "$(cat /tmp/cmp)"
fi

# --- compose: child widening attach is REJECTED ---
$N create --name child_widen \
    --attach 1,5 \
    --helpers 0x110 \
    -o test_child_widen.ns >/dev/null
if $N compose --child test_child_widen.ns --parent test_parent.ns \
              -o /tmp/x.ns >/tmp/cmp 2>&1; then
    no "compose accepted widening child (attach)"
else
    if grep -q 'widens attach' /tmp/cmp; then
        ok "compose rejects child widening attach"
    else
        no "compose error wrong" "$(cat /tmp/cmp)"
    fi
fi

# --- compose: child widening helper is REJECTED ---
$N create --name child_widen_h \
    --attach 1 \
    --helpers 0x110,0x303 \
    -o test_child_wh.ns >/dev/null
if $N compose --child test_child_wh.ns --parent test_parent.ns \
              -o /tmp/x.ns >/tmp/cmp 2>&1; then
    no "compose accepted widening child (helper)"
else
    if grep -q 'widens helper' /tmp/cmp; then
        ok "compose rejects child widening helper"
    else
        no "compose error wrong" "$(cat /tmp/cmp)"
    fi
fi

# --- compose: child widening quota is REJECTED ---
$N create --name child_widen_q \
    --attach 1 \
    --helpers 0x110 \
    --max-progs 20 \
    -o test_child_wq.ns >/dev/null
if $N compose --child test_child_wq.ns --parent test_parent.ns \
              -o /tmp/x.ns >/tmp/cmp 2>&1; then
    no "compose accepted widening child (quota)"
else
    if grep -q 'widens quota max_progs' /tmp/cmp; then
        ok "compose rejects child widening max_progs"
    else
        no "compose error wrong" "$(cat /tmp/cmp)"
    fi
fi

# --- post-compose, the effective namespace still rejects extras ---
$N check --ns test_eff.ns --attach 1 --helpers 0x201 >/tmp/c 2>&1
if grep -q 'helper_not_allowed' /tmp/c && grep -q '0x201' /tmp/c; then
    ok "effective ns drops parent's helper not in child"
else
    no "effective set wrong" "$(cat /tmp/c)"
fi

echo
echo "== Summary: $pass passed, $fail failed =="
exit $fail
