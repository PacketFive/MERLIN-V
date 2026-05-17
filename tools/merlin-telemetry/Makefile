# SPDX-License-Identifier: GPL-2.0-or-later
#
# Makefile - merlin-telemetry prototype.

TOPDIR        := ../..
DESIGN_UAPI   := $(TOPDIR)/docs/design/uapi
JIT_DIR       := ../merlin-jit-x86_64
VERIFIER_DIR  := ../merlin-verifier

CC      ?= gcc
CFLAGS  ?= -O2 -Wall -Wextra -Wformat=2 -Wshadow -Wno-unused-parameter
CFLAGS  += -I$(DESIGN_UAPI) -I$(JIT_DIR) -I$(VERIFIER_DIR) -I.
LDLIBS  := -lelf

TARGET  := merlin-telemetry
SHIM_OBJS := shim.o merlin-telemetry.o
JIT_OBJS  := $(JIT_DIR)/jit.c $(JIT_DIR)/emit.c $(JIT_DIR)/helpers.c
DEC_OBJ   := $(VERIFIER_DIR)/decode.c

.PHONY: all clean test mkfixture
all: $(TARGET)

# Build by linking against the JIT and verifier sources directly.
$(TARGET): $(SHIM_OBJS) jit.o emit.o helpers.o decode.o
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

%.o: %.c shim.h
	$(CC) $(CFLAGS) -c $< -o $@

jit.o: $(JIT_DIR)/jit.c
	$(CC) $(CFLAGS) -c $< -o $@

emit.o: $(JIT_DIR)/emit.c
	$(CC) $(CFLAGS) -c $< -o $@

helpers.o: $(JIT_DIR)/helpers.c
	$(CC) $(CFLAGS) -c $< -o $@

decode.o: $(DEC_OBJ)
	$(CC) $(CFLAGS) -c $< -o $@

mkfixture:
	$(MAKE) -C $(VERIFIER_DIR)/bad-progs mkfixture

test: $(TARGET) mkfixture
	@./run-tests.sh

clean:
	rm -f $(TARGET) $(SHIM_OBJS) jit.o emit.o helpers.o decode.o
	rm -f test_*.o test_*.bin
