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

# Callback-body variants: use -s cb.test section suffix so the verifier
# applies the Phase-3.A3 callback entry state (a0=scalar, a1=PTR_CTX).
assert_accept_cb () {
    local label="$1"; shift
    rm -f case_cb_${label}.o
    $MK -s cb.test case_cb_${label}.o "$@" >/dev/null
    if $VERIFIER case_cb_${label}.o >/tmp/out.cb_${label}.txt 2>/tmp/err.cb_${label}.txt; then
        echo "  [PASS] cb_$label  (accepted as expected)"
        pass=$((pass + 1))
    else
        echo "  [FAIL] cb_$label  (expected ACCEPT, got REJECT)"
        cat /tmp/err.cb_${label}.txt | sed 's/^/    /'
        fail=$((fail + 1))
    fi
}

assert_reject_cb () {
    local label="$1"; shift
    rm -f case_cb_${label}.o
    $MK -s cb.test case_cb_${label}.o "$@" >/dev/null
    if $VERIFIER case_cb_${label}.o >/tmp/out.cb_${label}.txt 2>/tmp/err.cb_${label}.txt; then
        echo "  [FAIL] cb_$label  (expected REJECT, got ACCEPT)"
        cat /tmp/out.cb_${label}.txt | sed 's/^/    /'
        fail=$((fail + 1))
    else
        local reason=$(grep -m1 'REJECT' /tmp/err.cb_${label}.txt || true)
        echo "  [PASS] cb_$label  (rejected as expected: ${reason#REJECT })"
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

# --- Phase-2 cases (CFG joins, bounded loops, pointer bounds) ----------

# Bounded loop accepted via merlin_loop_bound (helper id 0x0142):
#   addi a7, x0, 0x142   ; helper = loop-bound
#   addi a0, x0, 10      ; trip cap = 10
#   ecall                ; mark next block as a permitted loop header
#   addi a0, a0, -1      ; .L:  body: decrement counter
#   bne  a0, x0, .L      ;      branch back if non-zero
#   jalr x0, ra, 0       ; ret
#
# Encodings:
#   addi a7, x0, 0x142  = 0x14200893
#   addi a0, x0, 10     = 0x00a00513
#   ecall               = 0x00000073
#   addi a0, a0, -1     = 0xfff50513
#   bne  a0, x0, -4     = imm=-4 -> 0xfe051ee3
#   jalr x0, ra, 0      = 0x00008067
assert_accept "loop_bounded" \
    0x14200893 0x00a00513 0x00000073 \
    0xfff50513 0xfe051ee3 0x00008067

# Same back-edge but WITHOUT the loop-bound annotation -> rejected.
# (already covered by back_edge above, but include the symmetric
# negative for clarity)
assert_reject "loop_unbounded" \
    0xfff50513 0xfe051ee3 0x00008067

# Branch-and-merge with type-compatible scalars: program builds
# scalar 1 down one path, scalar 5 down the other, then returns.
# This exercises vstate_join() / scalar_join().
#   addi a7, x0, 0x110   ; ktime_get_ns
#   ecall                ; clobbers a0
#   addi a0, x0, 1       ; a0 = 1 (path A; cond never taken in practice)
#   beq  x0, x0, +8      ; jump over path B
#   addi a0, x0, 5       ; a0 = 5 (path B)
#   jalr x0, ra, 0       ; ret
#
# beq x0, x0, +8  = imm=8 -> 0x00000463
assert_accept "join_scalars" \
    0x11000893 0x00000073 \
    0x00100513 0x00000463 \
    0x00500513 0x00008067

# ALU on two pointers -> reject.
#   add s0, sp, a0   (sp = PTR_STACK, a0 = PTR_CTX at entry)
# add rd=s0(x8), rs1=sp(x2), rs2=a0(x10):
#   funct7=0 rs2=10 rs1=2 funct3=0 rd=8 op=0x33 = 0x00a10433
assert_reject "alu_two_ptrs" 0x00a10433 0x00008067

# --- Phase-3.A1 cases (stack discipline) -------------------------------

# Legal frame: addi sp, sp, -16; sw a0, 8(sp); lw a1, 8(sp);
#              addi sp, sp, 16; ret
assert_accept "stack_legal_frame" \
    0xff010113 0x00a12423 0x00812583 0x01010113 0x00008067

# Store above sp (offset 16 on a 16-byte frame leaves the frame):
# addi sp, sp, -16; sw a0, 16(sp); ret
assert_reject "stack_store_above_sp" \
    0xff010113 0x00a12823 0x00008067

# Frame budget overflow (default budget 512; allocate 1024):
# addi sp, sp, -1024; ret
assert_reject "stack_budget_overflow" \
    0xc0010113 0x00008067

# sp clobber: add sp, ra, ra; ret
assert_reject "stack_sp_clobber" \
    0x00108133 0x00008067

# --- Phase-3.A2 cases (kfunc resolution) -------------------------------

# Legal kfunc tail-call: resolve id 0x001 then jalr through a0.
#   addi a0, x0, 1          ; a0 = kfunc id 0x001
#   addi a7, x0, 0x143      ; MERLIN_HELPER_KFUNC_RESOLVE
#   ecall                   ; a0 -> PTR_KFUNC_SLOT(0x001)
#   jalr x0, a0, 0          ; tail-call
assert_accept "kfunc_call_legal" \
    0x00100513 0x14300893 0x00000073 0x00050067

# kfunc id NOT in the per-program allowlist (0x234).
assert_reject "kfunc_unallowed" \
    0x23400513 0x14300893 0x00000073 0x00050067

# Indirect jalr through an arbitrary scalar register.
#   addi t0, x0, 100; jalr x0, t0, 0
assert_reject "jalr_arbitrary_reg" \
    0x06400293 0x00028067

# Indirect jalr through ctx pointer (a0 holds PTR_CTX at entry).
#   jalr x0, a0, 0
assert_reject "jalr_unresolved_ptr" \
    0x00050067

# --- Phase-3.A3 cases (merlin_loop callback form) ----------------------

# Caller side: legal merlin_loop(5, cb_id=1, ctx).
#   addi a0, x0, 5          ; trip count N=5
#   addi a1, x0, 1          ; callback id 0x001 (in callback_allow)
#   addi a7, x0, 0x144      ; MERLIN_HELPER_LOOP_CB
#   ecall                   ; invoke loop_cb; next block = loop header
#   jalr x0, ra, 0          ; ret
#
# addi a0, x0, 5   = 0x00500513
# addi a1, x0, 1   = 0x00100593
# addi a7, x0, 0x144 = 0x14400893
assert_accept "loop_cb_caller_legal" \
    0x00500513 0x00100593 0x14400893 0x00000073 0x00008067

# Caller side: callback id 0x100 NOT in the per-program callback_allow.
#   addi a0, x0, 5          ; trip count
#   addi a1, x0, 0x100      ; cb id = 256 (not in allowlist)
#   addi a7, x0, 0x144      ; MERLIN_HELPER_LOOP_CB
#   ecall
#
# addi a1, x0, 0x100 = 0x10000593
assert_reject "loop_cb_caller_unregistered" \
    0x00500513 0x10000593 0x14400893 0x00000073 0x00008067

# Callback body: legal body — increment a0 (loop index), return.
#   At callback entry: a0 = scalar [0, INT64_MAX], a1 = PTR_CTX.
#   addi a0, a0, 1          ; scalar op on a0 — fine
#   jalr x0, ra, 0          ; ret
#
# addi a0, a0, 1 = 0x00150513
assert_accept_cb "loop_cb_body_legal" \
    0x00150513 0x00008067

# Callback body: load through a0, which is SCALAR at callback entry.
#   At callback entry a0 is a scalar (loop index), NOT a pointer.
#   Attempting lw t1, 0(a0) must be rejected.
#   lw t1, 0(a0) = 0x00052303
assert_reject_cb "loop_cb_body_load_a0" \
    0x00052303 0x00008067

echo
echo "== Summary: $pass passed, $fail failed =="
exit $fail
