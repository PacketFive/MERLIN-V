# SPDX-License-Identifier: Apache-2.0
#
# Build the Icicle Kit sample-classifier user-space driver.
#
# Cross-compile from x86 host:
#   make CROSS_COMPILE=riscv64-unknown-linux-gnu-
#
# Native build on the Icicle:
#   make

CC      ?= $(CROSS_COMPILE)cc
CFLAGS  := -std=c11 -O2 -Wall -Wextra -Wno-unused-parameter

classifier: src/main.c src/classifier_blob.c
	$(CC) $(CFLAGS) -o $@ $^

.PHONY: test clean
test: classifier
	@echo "Run as root on a board with /dev/merlin present:"
	@echo "  sudo ./classifier"

clean:
	rm -f classifier
