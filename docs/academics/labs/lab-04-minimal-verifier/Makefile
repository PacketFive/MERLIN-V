# SPDX-License-Identifier: GPL-2.0-or-later
# PROVIDED — do not modify.

CC      ?= cc
CFLAGS  := -std=c11 -O2 -Wall -Wextra -Wno-unused-parameter \
           -I../common -Isrc -I../lab-03-userland-interp/src

OBJS := src/verify.o src/main.o ../common/elf.o ../lab-03-userland-interp/src/decode.o

merlin-verify: $(OBJS)
	$(CC) $(CFLAGS) -o $@ $(OBJS)

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

.PHONY: test grade clean
test: merlin-verify
	@./tests/run-all.sh

grade:
	@../autograder/runner.sh lab-04

clean:
	rm -f merlin-verify $(OBJS)
