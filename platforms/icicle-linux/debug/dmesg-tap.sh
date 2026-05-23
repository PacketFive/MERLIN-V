#!/bin/sh
# SPDX-License-Identifier: Apache-2.0
#
# debug/dmesg-tap.sh — live-tap the kernel log filtered to MERLIN-V events.
#
# Usage:  ./dmesg-tap.sh
#
# Useful while iterating on the sample-classifier user-space driver:
# pin one terminal to this, run `./classifier` in another, watch the
# kernel-side trace appear in real time.

exec dmesg -wT | grep --line-buffered -i 'merlin'
