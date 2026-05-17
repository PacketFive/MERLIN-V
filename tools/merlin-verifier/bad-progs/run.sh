#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# run.sh - drive merlin-verifier through positive + negative fixtures.
#
# Each case is a function name; the case sets EXPECT to "accept" or
# "reject" and INSNS to a space-separated list of 32-bit hex literals,
# then calls run_case.

set -u

VERIFIER=../merlin-verifier
MK=./mkfixture
pass=0
fail=0

# ----------------------------------------------------------------------
# RV64 encodings reference:
#   addi a7, x0, K                op=13, funct3=0, rd=17, rs1=0, imm=K
#   ecall                         = 0x00000073
#   ebreak                        = 0x00100073
#   jalr  x0, ra, 0   (= ret)     op=67, rd=0, funct3=0, rs1=1, imm=0
#                                 = 0x00008067
#   lw    t1, 0(t0)               op=03, funct3=2, rd=6, rs1=5, imm=0
#                                 = 0x0002a303
#   csrrw x0, mstatus, x0         op=73, funct3=1, rd=0, rs1=0, csr=0x300
#                                 = 0x30001073
# ----------------------------------------------------------------------

assert_accept () {
    local label="$1"; shift
    rm -f case_${label}.o
    $MK case_${label}.o "$@" >/dev/null
    if $VERIFIER case_${label}.o >/tmp/out.${label}.txt 2>/tmp/err.${label}.txt; then
        echo "  [PASS] $label  (accepted as expected)"
        pass=$((pass + 1))
    else
        echo "  [FAIL] $label  (expected ACCEPT, got REJECT)"
        cat /tmp/err.${label}.txt | sed 's/^/    /'
        fail=$((fail + 1))
    fi
}

assert_reject () {
    local label="$1"; shift
    rm -f case_${label}.o
    $MK case_${label}.o "$@" >/dev/null
    if $VERIFIER case_${label}.o >/tmp/out.${label}.txt 2>/tmp/err.${label}.txt; then
        echo "  [FAIL] $label  (expected REJECT, got ACCEPT)"
        cat /tmp/out.${label}.txt | sed 's/^/    /'
        fail=$((fail + 1))
    else
        local reason=$(grep -m1 'REJECT' /tmp/err.${label}.txt || true)
        echo "  [PASS] $label  (rejected as expected: ${reason#REJECT })"
        pass=$((pass + 1))
    fi
}

echo "== merlin-verifier test battery =="

# --- positive cases -----------------------------------------------------

# Pure helper call: addi a7, x0, 0x201 (mvdp_redirect); ecall; ret
assert_accept "helper_redirect" 0x20100893 0x00000073 0x00008067

# Trivial program: just return MVDP_DROP (a0 = 1; ret)
# addi a0, x0, 1 = 0x00100513
# jalr x0, ra, 0 = 0x00008067
assert_accept "drop_only" 0x00100513 0x00008067

# Two helpers in sequence
# addi a7, x0, 0x110 (ktime_get_ns); ecall;
# addi a7, x0, 0x201 (mvdp_redirect); ecall; ret
assert_accept "two_helpers" 0x11000893 0x00000073 0x20100893 0x00000073 0x00008067

# --- negative cases -----------------------------------------------------

# ebreak (forbidden instruction class)
assert_reject "ebreak"      0x00100073 0x00008067

# CSR write (forbidden)
assert_reject "csrrw"       0x30001073 0x00008067

# ecall with no preceding a7 load (a7 starts UNKNOWN at function entry)
assert_reject "ecall_bare"  0x00000073 0x00008067

# ecall with helper id NOT in the MVDP allowlist (0x301 = tracing-only)
assert_reject "ecall_disallowed" 0x30100893 0x00000073 0x00008067

# Load through unknown reg (t0=x5 is UNKNOWN at entry; lw t1, 0(t0))
assert_reject "load_unknown" 0x0002a303 0x00008067

# Back-edge branch (beq x0,x0,-4)
assert_reject "back_edge"   0x00000013 0xFE000EE3

# Bare ecall with no helper at end of program (a7 known to be ALU result,
# not constant from li/addi-from-x0)
# add a7, t0, t0 = funct7=0, rs2=5, rs1=5, funct3=0, rd=17, op=33
# = 0x005288b3
assert_reject "ecall_a7_nonconst" 0x005288b3 0x00000073

echo
echo "== Summary: $pass passed, $fail failed =="
exit $fail
