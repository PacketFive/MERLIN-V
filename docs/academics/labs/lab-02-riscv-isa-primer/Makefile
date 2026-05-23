# SPDX-License-Identifier: GPL-2.0-or-later
# PROVIDED — do not modify.

CC      ?= cc
CFLAGS  := -std=c11 -O2 -Wall -Wextra -Wno-unused-parameter \
           -I../common

exercises: exercises.c ../common/rv32.h ../common/test_helpers.h
	$(CC) $(CFLAGS) -o $@ exercises.c

.PHONY: test grade clean
test: exercises
	./exercises

grade:
	../autograder/runner.sh lab-02

clean:
	rm -f exercises
