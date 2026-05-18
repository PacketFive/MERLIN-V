# SPDX-License-Identifier: Apache-2.0
#
# Host smoke test for the MERLIN-V Zephyr runtime sources.
#
# Compiles decode.c + verifier.c + a host loader stub against plain libc.
# Verifies that the hello-merlin sample blob parses + verifies.

CC      ?= cc
CFLAGS  := -std=c11 -O2 -Wall -Wextra -Wno-unused-parameter \
           -I../include -I../src -DMERLIN_HOST_SMOKE

OBJS := decode.host.o verifier.host.o smoke.o sample_blob.host.o

.PHONY: all run clean
all: smoke

smoke: $(OBJS)
	$(CC) $(CFLAGS) -o $@ $(OBJS)

decode.host.o: ../src/decode.c
	$(CC) $(CFLAGS) -c -o $@ $<

verifier.host.o: ../src/verifier.c
	$(CC) $(CFLAGS) -c -o $@ $<

sample_blob.host.o: ../samples/hello-merlin/src/sample_blob.c
	$(CC) $(CFLAGS) -c -o $@ $<

smoke.o: smoke.c
	$(CC) $(CFLAGS) -c -o $@ $<

run: smoke
	./smoke

clean:
	rm -f smoke *.o
