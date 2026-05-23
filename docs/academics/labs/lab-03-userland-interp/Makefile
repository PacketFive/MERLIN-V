# SPDX-License-Identifier: GPL-2.0-or-later
# PROVIDED — do not modify.

CC      ?= cc
CFLAGS  := -std=c11 -O2 -Wall -Wextra -Wno-unused-parameter \
           -I../common -Isrc

OBJS := src/interp.o src/decode.o src/main.o ../common/elf.o

merlin: $(OBJS)
	$(CC) $(CFLAGS) -o $@ $(OBJS)

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

.PHONY: test grade clean
test: merlin
	@./tests/run-all.sh

grade:
	@../autograder/runner.sh lab-03

clean:
	rm -f merlin $(OBJS)
