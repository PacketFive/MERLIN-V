# SPDX-License-Identifier: GPL-2.0-or-later
#
# Makefile - merlin-txn prototype.

TOPDIR      := ../..
DESIGN_UAPI := $(TOPDIR)/docs/design/uapi

CC      ?= gcc
CFLAGS  ?= -O2 -Wall -Wextra -Wformat=2 -Wshadow -Wno-unused-parameter
CFLAGS  += -I$(DESIGN_UAPI) -I.
LDLIBS  :=

TARGET  := merlin-txn
SRCS    := merlin-txn.c store.c
OBJS    := $(SRCS:.c=.o)

.PHONY: all clean test
all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

%.o: %.c store.h
	$(CC) $(CFLAGS) -c $< -o $@

test: $(TARGET)
	@./run-tests.sh

clean:
	rm -f $(TARGET) $(OBJS) test_*.txn
