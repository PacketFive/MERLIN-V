#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# Test battery for merlin-telemetry.

set -u

TEL="./merlin-telemetry"
MK="../merlin-verifier/bad-progs/mkfixture"
pass=0; fail=0

ok () { echo "  [PASS] $1"; pass=$((pass+1)); }
no () { echo "  [FAIL] $1"; [ -n "${2-}" ] && echo "$2" | sed 's/^/    /'; fail=$((fail+1)); }

# Fixture: addi a0,x0,1; ret  (returns MVDP_DROP = 1)
$MK test_drop.o 0x00100513 0x00008067 >/dev/null

# Fixture: two helpers + ret returning 0x4edcab1e (mvdp_redirect stub)
$MK test_two_h.o 0x11000893 0x00000073 0x20100893 0x00000073 0x00008067 >/dev/null

echo "== merlin-telemetry test battery =="

# --- run drop 7 times, expect run_count=7, verdict[1]=7 ---
out=$($TEL run -n 7 test_drop.o)
rc=$?
if [ $rc -ne 0 ]; then
    no "drop run rc=$rc" "$out"
else
    rc1=$(echo "$out" | grep '^run_count:' | awk '{print $2}')
    v1=$(echo "$out"  | grep '^verdict\[1:DROP\]:' | awk '{print $2}')
    if [ "$rc1" = "7" ] && [ "$v1" = "7" ]; then
        ok "drop run_count=7 verdict[1]=7"
    else
        no "drop counters wrong (run_count=$rc1 verdict[1]=$v1)" "$out"
    fi
fi

# --- run with helpers: helper_call_count should be 2*N ---
N=5
out=$($TEL run -n $N test_two_h.o)
rc=$?
if [ $rc -ne 0 ]; then
    no "two_helpers run rc=$rc" "$out"
else
    hc=$(echo "$out" | grep '^helper_call_count:'  | awk '{print $2}')
    rc2=$(echo "$out" | grep '^run_count:' | awk '{print $2}')
    expect_hc=$((N * 2))
    if [ "$rc2" = "$N" ] && [ "$hc" = "$expect_hc" ]; then
        ok "two_helpers run_count=$N helper_call_count=$hc"
    else
        no "two_helpers counters wrong (run_count=$rc2 helper_call_count=$hc; expected $N/$expect_hc)" "$out"
    fi
fi

# --- run_ns_total grows monotonically with iters ---
out1=$($TEL run -n 1   test_drop.o | grep '^run_ns_total:' | awk '{print $2}')
out2=$($TEL run -n 100 test_drop.o | grep '^run_ns_total:' | awk '{print $2}')
if [ "$out2" -ge "$out1" ] 2>/dev/null && [ "$out2" -gt 0 ]; then
    ok "run_ns_total grows ($out1 ns @ 1 iter, $out2 ns @ 100 iters)"
else
    no "run_ns_total didn't grow" "1=$out1 100=$out2"
fi

# --- snapshot wire format: write to file, read back, verify ---
# We don't expose a dedicated 'snapshot' subcommand for v0; instead,
# build a tiny snapshot binary in shell using printf and check that
# dump parses it forward-compatibly with a known size+payload.
python3 -c "
import struct
size = 32 + 8*8 + 8*4 + 8*4   # 8 + run_count(8) + run_ns(8) + 8*8 verdict + 4*8 misc + 4*8 reserved = 128
size_actual = 8 + 8 + 8 + 8*8 + 8 + 8 + 8 + 8 + 8*4
buf = struct.pack('<IIQQ' + 'Q'*8 + 'QQQQ' + 'Q'*4,
    size_actual, 0,
    111, 222000,
    1, 2, 3, 4, 5, 6, 0, 0,
    10, 0, 999, 12345,
    0, 0, 0, 0)
open('test_snap.bin','wb').write(buf)
"
out=$($TEL dump test_snap.bin 2>&1)
rc1=$(echo "$out" | grep '^run_count' | awk '{print $2}')
hc=$(echo "$out"  | grep '^helper_call_count' | awk '{print $2}')
v3=$(echo "$out"  | grep '^verdict\[3\]' | awk '{print $2}')
if [ "$rc1" = "111" ] && [ "$hc" = "10" ] && [ "$v3" = "4" ]; then
    ok "dump parses wire format (run_count=111 verdict[3]=4 helper_call_count=10)"
else
    no "dump parsing wrong" "$out"
fi

# --- text mode produces tracefs-style format with verdict labels ---
text=$($TEL text test_snap.bin 2>&1)
if echo "$text" | grep -q 'verdict\[1:DROP\]:    2' && \
   echo "$text" | grep -q 'verdict\[2:PASS\]:    3'; then
    ok "text format labels verdicts (DROP=2, PASS=3)"
else
    no "text format mislabel" "$text"
fi

# --- forward-compat: write a stats blob whose advertised size is
#     LARGER than what we know (simulating a future kernel that
#     added fields).  Reader must consult `size` and still parse
#     correctly. ---
python3 -c "
import struct
# Advertise size 200 but we only emit our 128-byte payload + 72 zero bytes.
size = 200
buf = struct.pack('<IIQQ' + 'Q'*8 + 'QQQQ' + 'Q'*4,
    size, 0,
    77, 888,
    0, 5, 0, 0, 0, 0, 0, 0,
    3, 0, 0, 0,
    0, 0, 0, 0)
buf += b'\\x00' * (200 - len(buf))
open('test_future.bin','wb').write(buf)
"
out=$($TEL dump test_future.bin 2>&1)
if [ $? -ne 0 ]; then
    no "future-size parse failed" "$out"
else
    rc=$(echo "$out" | grep '^run_count' | awk '{print $2}')
    if [ "$rc" = "77" ]; then
        ok "forward-compat: future kernel size accepted; known fields read"
    else
        no "forward-compat parse wrong (run_count=$rc)" "$out"
    fi
fi

echo
echo "== Summary: $pass passed, $fail failed =="
exit $fail
