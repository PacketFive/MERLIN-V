# SPDX-License-Identifier: GPL-2.0-or-later
#
# Makefile - merlin-attest prototype.

TOPDIR      := ../..
DESIGN_UAPI := $(TOPDIR)/docs/design/uapi

CC      ?= gcc
CFLAGS  ?= -O2 -Wall -Wextra -Wformat=2 -Wshadow -Wno-unused-parameter
CFLAGS  += -I$(DESIGN_UAPI) -I.
LDLIBS  := -lcrypto

TARGET  := merlin-attest
SRCS    := merlin-attest.c mak.c
OBJS    := $(SRCS:.c=.o)

.PHONY: all clean test
all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

%.o: %.c mak.h
	$(CC) $(CFLAGS) -c $< -o $@

test: $(TARGET)
	@./run-tests.sh

clean:
	rm -f $(TARGET) $(OBJS)
	rm -rf test_mak
	rm -f test_*.bin
