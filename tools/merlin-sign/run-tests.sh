#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# Test battery for merlin-sign.
#
# Builds a small RV64 .o, signs it with ed25519, then runs a series
# of positive and negative verify cases.  Uses the OpenSSL CLI to
# generate the keypair so we don't carry binary key material in
# the repo.

set -u

SIGN="./merlin-sign"
MK="../merlin-verifier/bad-progs/mkfixture"
pass=0; fail=0

# Two distinct keypairs so we can test the "wrong key" case.
KEY_A_PRIV=test_keyA.priv.pem
KEY_A_PUB=test_keyA.pub.pem
KEY_B_PRIV=test_keyB.priv.pem
KEY_B_PUB=test_keyB.pub.pem

ok () {
    echo "  [PASS] $1"
    pass=$((pass + 1))
}
no () {
    echo "  [FAIL] $1"
    [ -n "${2-}" ] && echo "$2" | sed 's/^/    /'
    fail=$((fail + 1))
}

setup () {
    rm -f test_*.o test_*.pem
    # ed25519 keys
    openssl genpkey -algorithm ed25519 -out $KEY_A_PRIV >/dev/null 2>&1
    openssl pkey -in $KEY_A_PRIV -pubout -out $KEY_A_PUB >/dev/null 2>&1
    openssl genpkey -algorithm ed25519 -out $KEY_B_PRIV >/dev/null 2>&1
    openssl pkey -in $KEY_B_PRIV -pubout -out $KEY_B_PUB >/dev/null 2>&1

    # Two helpers + ret  -- a non-trivial fixture
    $MK test_prog.o 0x11000893 0x00000073 0x20100893 0x00000073 0x00008067 >/dev/null
}

echo "== merlin-sign test battery =="
setup

# --- Case 1: tag is deterministic ---
TAG1=$($SIGN tag test_prog.o | awk '{print $1}')
TAG2=$($SIGN tag test_prog.o | awk '{print $1}')
if [ -n "$TAG1" ] && [ "$TAG1" = "$TAG2" ]; then
    ok "tag deterministic ($TAG1)"
else
    no "tag deterministic" "TAG1=$TAG1  TAG2=$TAG2"
fi

# --- Case 2: merlin-sign tag matches merlin-objtool sha256 ---
if [ -x ../merlin-objtool/merlin-objtool ]; then
    OBJTAG=$(../merlin-objtool/merlin-objtool sha256 test_prog.o | awk '{print $1}')
    if [ "$TAG1" = "$OBJTAG" ]; then
        ok "tag matches merlin-objtool sha256 ($OBJTAG)"
    else
        no "tag matches merlin-objtool sha256" "sign=$TAG1  objtool=$OBJTAG"
    fi
else
    echo "  [SKIP] merlin-objtool comparison (objtool not built)"
fi

# --- Case 3: sign + verify round-trip ---
if $SIGN sign -k $KEY_A_PRIV -i 0x1001 test_prog.o test_prog.signed.o >/tmp/sign_out 2>&1 ; then
    if $SIGN verify -k $KEY_A_PUB test_prog.signed.o >/tmp/v_out 2>&1 ; then
        ok "sign+verify with matching keys"
    else
        no "sign+verify with matching keys" "$(cat /tmp/v_out)"
    fi
else
    no "sign step itself" "$(cat /tmp/sign_out)"
fi

# --- Case 4: signed file tag unchanged after embedding ---
# (.merlin.sig itself is NOT in the canonical signed-section list, so
# adding it must not change the hash.)
TAG_AFTER=$($SIGN tag test_prog.signed.o | awk '{print $1}')
if [ "$TAG_AFTER" = "$TAG1" ]; then
    ok "tag unchanged after signing (signed-region excludes .merlin.sig)"
else
    no "tag unchanged after signing" "before=$TAG1  after=$TAG_AFTER"
fi

# --- Case 5: verify FAILS with wrong public key ---
if $SIGN verify -k $KEY_B_PUB test_prog.signed.o >/tmp/v_out 2>&1 ; then
    no "verify with wrong key (should reject)" "$(cat /tmp/v_out)"
else
    ok "verify with wrong key rejected"
fi

# --- Case 6: verify FAILS after content mutation ---
cp test_prog.signed.o test_prog.tampered.o
# Find offset of "GPL\0" in .merlin.license; flip a byte.
OFF=$(grep -obaP '\x00GPL\x00' test_prog.tampered.o | head -1 | cut -d: -f1)
if [ -n "$OFF" ]; then
    OFF=$((OFF + 1))   # skip past leading NUL onto the 'G'
    printf 'X' | dd of=test_prog.tampered.o bs=1 seek=$OFF count=1 conv=notrunc status=none
    if $SIGN verify -k $KEY_A_PUB test_prog.tampered.o >/tmp/v_out 2>&1 ; then
        no "verify rejects content mutation"
    else
        ok "verify rejects content mutation"
    fi
else
    echo "  [SKIP] mutation case (could not locate license bytes)"
fi

# --- Case 7: verify FAILS on unsigned file ---
if $SIGN verify -k $KEY_A_PUB test_prog.o >/tmp/v_out 2>&1 ; then
    no "verify rejects unsigned object"
else
    ok "verify rejects unsigned object"
fi

# --- Case 8: dump prints expected fields ---
DUMP=$($SIGN dump test_prog.signed.o)
if echo "$DUMP" | grep -q 'algo.*ed25519' && echo "$DUMP" | grep -q 'sig_bytes_len    = 64' ; then
    ok "dump prints algo + sig_bytes_len"
else
    no "dump prints algo + sig_bytes_len" "$DUMP"
fi

echo
echo "== Summary: $pass passed, $fail failed =="
exit $fail
