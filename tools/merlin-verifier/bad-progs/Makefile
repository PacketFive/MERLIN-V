# SPDX-License-Identifier: GPL-2.0-or-later
#
# bad-progs/Makefile - negative-test fixtures for merlin-verifier.

TOPDIR      := ../../..
DESIGN_UAPI := $(TOPDIR)/docs/design/uapi
CC          ?= gcc
CFLAGS      ?= -O2 -Wall -Wextra
CFLAGS      += -I$(DESIGN_UAPI)
LDLIBS      := -lelf

VERIFIER    := ../merlin-verifier

.PHONY: all clean test
all: mkfixture

mkfixture: mkfixture.c
	$(CC) $(CFLAGS) -o $@ $< $(LDLIBS)

# Run a battery of hand-assembled fixtures and assert verifier
# verdicts.  Each case is:
#
#    case <label> <expect-accept|expect-reject> <opcode> ...
#
# We invoke mkfixture to materialise a tiny .o then drive the
# verifier through ./run.sh.
test: mkfixture $(VERIFIER)
	./run.sh

clean:
	rm -f mkfixture *.merlin.o
