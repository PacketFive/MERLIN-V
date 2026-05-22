# SPDX-License-Identifier: Apache-2.0
#
# host/Makefile — userland AXI-Lite loader for the fabric core.

CC      ?= $(CROSS_COMPILE)cc
CFLAGS  := -std=c11 -O2 -Wall -Wextra

merlin-fabric-load: merlin-fabric-load.c
	$(CC) $(CFLAGS) -o $@ $<

.PHONY: clean
clean:
	rm -f merlin-fabric-load
