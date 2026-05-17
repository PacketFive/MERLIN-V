#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# Test battery for merlin-attest.

set -u

A="./merlin-attest"
export MERLIN_MAK_DIR=$(pwd)/test_mak
rm -rf "$MERLIN_MAK_DIR"
pass=0; fail=0

ok () { echo "  [PASS] $1"; pass=$((pass+1)); }
no () { echo "  [FAIL] $1"; [ -n "${2-}" ] && echo "$2" | sed 's/^/    /'; fail=$((fail+1)); }

# Fixed 32-byte nonce + tag for deterministic tests.
NONCE_A=$(printf '%064x' 1)
NONCE_B=$(printf '%064x' 2)
TAG_X=$(printf '%064x' 0xdeadbeef)
TAG_Y=$(printf '%064x' 0xcafe)

echo "== merlin-attest test battery =="

# --- init creates the MAK; pub-hash is non-zero ---
out=$($A init 2>&1)
if echo "$out" | grep -q 'MAK pub-hash:' ; then
    PUB="$MERLIN_MAK_DIR/mak.pub.pem"
    if [ -f "$PUB" ] && [ -f "$MERLIN_MAK_DIR/mak.priv.pem" ]; then
        ok "init creates MAK keys + pub-hash"
    else
        no "MAK files missing after init" "$out"
    fi
else
    no "init output" "$out"
fi

# --- init is idempotent ---
out2=$($A init 2>&1)
# pub-hash should be the same across runs
H1=$(echo "$out"  | awk '/MAK pub-hash:/ {print $3}')
H2=$(echo "$out2" | awk '/MAK pub-hash:/ {print $3}')
if [ -n "$H1" ] && [ "$H1" = "$H2" ]; then
    ok "init idempotent ($H1)"
else
    no "init not idempotent" "before=$H1  after=$H2"
fi

# --- quote + verify round-trip with matching nonce ---
$A quote --prog-id 7 --ns-id 1 --profile 1 --load-time-ns 123 \
         --prog-tag "$TAG_X" --nonce "$NONCE_A" \
         -o test_q1.bin > /tmp/q1_out 2>&1
if [ $? -ne 0 ]; then
    no "quote sign" "$(cat /tmp/q1_out)"
else
    if $A verify --pub "$PUB" --nonce "$NONCE_A" test_q1.bin >/tmp/v_out 2>&1 ; then
        ok "round-trip: sign + verify with matching nonce"
    else
        no "round-trip verify failed" "$(cat /tmp/v_out)"
    fi
fi

# --- verify FAILS with wrong nonce ---
if $A verify --pub "$PUB" --nonce "$NONCE_B" test_q1.bin >/tmp/v_out 2>&1 ; then
    no "verify with wrong nonce (should reject)"
else
    ok "verify rejects wrong nonce"
fi

# --- quote produces a fresh seq each time ---
$A quote --prog-id 1 --prog-tag "$TAG_X" --nonce "$NONCE_A" -o test_q2.bin >/dev/null
$A quote --prog-id 1 --prog-tag "$TAG_X" --nonce "$NONCE_A" -o test_q3.bin >/dev/null
SEQ2=$($A dump test_q2.bin | awk '/quote_seq/ {print $3}')
SEQ3=$($A dump test_q3.bin | awk '/quote_seq/ {print $3}')
if [ "$SEQ3" -gt "$SEQ2" ] 2>/dev/null; then
    ok "quote_seq strictly increases ($SEQ2 -> $SEQ3)"
else
    no "quote_seq not monotonic" "seq2=$SEQ2  seq3=$SEQ3"
fi

# --- replay defence: --last-seq >= quote.seq must reject ---
HIGH=$SEQ3
if $A verify --pub "$PUB" --nonce "$NONCE_A" --last-seq $HIGH test_q3.bin >/tmp/v_out 2>&1 ; then
    no "verify with last-seq == quote.seq (should reject)"
else
    ok "verify rejects when last-seq >= quote.seq (replay defence)"
fi

# --- replay defence: --last-seq < quote.seq must accept ---
LOW=$((SEQ3 - 1))
if $A verify --pub "$PUB" --nonce "$NONCE_A" --last-seq $LOW test_q3.bin >/tmp/v_out 2>&1 ; then
    ok "verify accepts when last-seq < quote.seq"
else
    no "verify rejected with last-seq < quote.seq" "$(cat /tmp/v_out)"
fi

# --- verify FAILS with wrong public key ---
# Create a second MAK in a separate dir and try verifying q1 with its pub.
MERLIN_MAK_DIR_OLD="$MERLIN_MAK_DIR"
export MERLIN_MAK_DIR="$MERLIN_MAK_DIR.alt"
rm -rf "$MERLIN_MAK_DIR"
$A init >/dev/null
PUB_ALT="$MERLIN_MAK_DIR/mak.pub.pem"
export MERLIN_MAK_DIR="$MERLIN_MAK_DIR_OLD"
if $A verify --pub "$PUB_ALT" --nonce "$NONCE_A" test_q1.bin >/tmp/v_out 2>&1 ; then
    no "verify with wrong pub (should reject)"
else
    ok "verify rejects wrong public key"
fi

# --- mutate quote bytes (flip one byte in prog_tag); verify fails ---
cp test_q1.bin test_q1.tampered.bin
# prog_tag offset: 4+4+4+4 + 4+4+4+4 = 32; flip byte at offset 33 (inside tag)
printf 'Z' | dd of=test_q1.tampered.bin bs=1 seek=33 count=1 conv=notrunc status=none
if $A verify --pub "$PUB" --nonce "$NONCE_A" test_q1.tampered.bin >/tmp/v_out 2>&1 ; then
    no "verify rejects content mutation"
else
    ok "verify rejects content mutation"
fi

# --- dump prints expected fields ---
DUMP=$($A dump test_q1.bin)
if echo "$DUMP" | grep -q 'algo.*ed25519' && \
   echo "$DUMP" | grep -q "prog_tag.*$TAG_X" ; then
    ok "dump prints algo + prog_tag"
else
    no "dump output" "$DUMP"
fi

echo
echo "== Summary: $pass passed, $fail failed =="
exit $fail
