#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# run-tests.sh - end-to-end JIT test battery.
#
# Each case builds a tiny RV64 MERLIN-V .o with mkfixture, runs the
# JIT, and asserts on (return value, helper-call count).

set -u

JIT="./merlin-jit"
MK="../merlin-verifier/bad-progs/mkfixture"
pass=0
fail=0

# ----------------------------------------------------------------------
# RV64 encodings used below:
#
#   addi a0, x0, 1                = 0x00100513
#   addi a0, x0, 0xFF             = 0x0FF00513
#   addi a7, x0, 0x110            = 0x11000893
#   addi a7, x0, 0x201            = 0x20100893
#   ecall                         = 0x00000073
#   jalr  x0, ra, 0   (ret)       = 0x00008067
#
# The verifier already accepts each of these in its test battery; we
# now check that the JIT translates them into x86_64 that, when run,
# returns the expected value.
# ----------------------------------------------------------------------

run_case () {
    local label="$1"; shift
    local expect_ret="$1"; shift
    local expect_helpers="$1"; shift   # space-separated list "0x110 0x201"

    rm -f test_${label}.o
    $MK test_${label}.o "$@" >/dev/null

    out=$($JIT test_${label}.o 2>&1)
    rc=$?
    if [ $rc -ne 0 ]; then
        echo "  [FAIL] $label  (JIT exit $rc)"
        echo "$out" | sed 's/^/    /'
        fail=$((fail+1))
        return
    fi

    got=$(echo "$out" | sed -n 's/.*return: \(0x[0-9a-f]*\).*/\1/p')
    if [ "$got" != "$expect_ret" ]; then
        echo "  [FAIL] $label  (expected ret=$expect_ret got=$got)"
        echo "$out" | sed 's/^/    /'
        fail=$((fail+1))
        return
    fi

    ok=1
    for h in $expect_helpers; do
        if ! echo "$out" | grep -q "id $h called"; then
            echo "  [FAIL] $label  (expected helper $h not called)"
            echo "$out" | sed 's/^/    /'
            fail=$((fail+1))
            ok=0
            break
        fi
    done
    [ $ok -eq 1 ] || return

    echo "  [PASS] $label  (ret=$got, helpers=[${expect_helpers:-none}])"
    pass=$((pass+1))
}

echo "== merlin-jit end-to-end test battery =="

# ---- Pure-arithmetic programs (no helpers; return a0) ----

# Program: addi a0, x0, 1; ret    ->  return 1 (MVDP_DROP)
run_case "drop"      0x1   ""  0x00100513 0x00008067

# Program: addi a0, x0, 0xFF; ret  -> return 0xFF
run_case "ret255"    0xff  ""  0x0FF00513 0x00008067

# Program: addi a0, x0, 0; ret   -> return 0
run_case "ret0"      0x0   ""  0x00000513 0x00008067

# ---- Programs that exercise the helper trampoline ----

# helper_redirect: addi a7, x0, 0x201; ecall; ret
# trampoline writes a0 = 0x4EDCAB1E (stub for mvdp_redirect)
run_case "helper_redirect"  0x4edcab1e  "0x201"  0x20100893 0x00000073 0x00008067

# ktime_get_ns: addi a7, x0, 0x110; ecall; ret
# trampoline writes a0 = 0xCAFE0000 (stub for ktime_get_ns)
run_case "ktime"            0xcafe0000  "0x110"  0x11000893 0x00000073 0x00008067

# Two helpers in sequence: last one wins on a0
# addi a7, x0, 0x110; ecall;     a0 := 0xCAFE0000
# addi a7, x0, 0x201; ecall;     a0 := 0x4EDCAB1E
# ret                                 -> return 0x4EDCAB1E
run_case "two_helpers"      0x4edcab1e  "0x110 0x201"  0x11000893 0x00000073 0x20100893 0x00000073 0x00008067

echo
echo "== Summary: $pass passed, $fail failed =="
exit $fail
