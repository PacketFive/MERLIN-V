# SPDX-License-Identifier: GPL-2.0-or-later
#
# Makefile - merlin-sign build.
#
# Dependencies: libelf-dev, libssl-dev, plus OpenSSL CLI for tests.

CC      ?= gcc
CFLAGS  ?= -O2 -Wall -Wextra -Wformat=2 -Wshadow -Wno-unused-parameter
LDLIBS  := -lelf -lcrypto

TARGET  := merlin-sign
SRCS    := merlin-sign.c sig.c
OBJS    := $(SRCS:.c=.o)

MK_FIX  := ../merlin-verifier/bad-progs/mkfixture
OBJTOOL := ../merlin-objtool/merlin-objtool

.PHONY: all clean test mkfixture
all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

%.o: %.c sig.h
	$(CC) $(CFLAGS) -c $< -o $@

mkfixture:
	$(MAKE) -C ../merlin-verifier/bad-progs mkfixture

test: $(TARGET) mkfixture
	@./run-tests.sh

clean:
	rm -f $(TARGET) $(OBJS)
	rm -f test_*.o test_*.signed.o test_*.pem
