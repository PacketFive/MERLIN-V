# SPDX-License-Identifier: GPL-2.0-or-later
#
# Makefile - prototype merlin-jit-x86_64 build.
#
# Dependency:  sudo apt install libelf-dev

TOPDIR       := ../..
DESIGN_UAPI  := $(TOPDIR)/docs/design/uapi
VERIFIER_DIR := ../merlin-verifier
MKFIXTURE    := $(VERIFIER_DIR)/bad-progs/mkfixture

CC      ?= gcc
CFLAGS  ?= -O2 -Wall -Wextra -Wformat=2 -Wshadow -Wno-unused-parameter
CFLAGS  += -I$(DESIGN_UAPI) -I$(VERIFIER_DIR)
LDLIBS  := -lelf

TARGET  := merlin-jit
SRCS    := merlin-jit.c jit.c emit.c helpers.c
OBJS    := $(SRCS:.c=.o)

# Share the decoder with merlin-verifier rather than maintain two copies.
OBJS    += decode.o

.PHONY: all clean test mkfixture
all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

%.o: %.c jit.h emit.h
	$(CC) $(CFLAGS) -c $< -o $@

decode.o: $(VERIFIER_DIR)/decode.c $(VERIFIER_DIR)/decode.h
	$(CC) $(CFLAGS) -c $< -o $@

mkfixture:
	$(MAKE) -C $(VERIFIER_DIR)/bad-progs mkfixture

test: $(TARGET) mkfixture
	@./run-tests.sh

clean:
	rm -f $(TARGET) $(OBJS)
	rm -f test_*.o
