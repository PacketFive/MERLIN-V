# SPDX-License-Identifier: GPL-2.0-or-later
#
# tools/merlin-watcher/Makefile

CC      ?= cc
CFLAGS  := -std=c11 -O2 -Wall -Wextra -Wno-unused-parameter

PKGS := libnl-genl-3.0
PKG_CFLAGS := $(shell pkg-config --cflags $(PKGS) 2>/dev/null)
PKG_LIBS   := $(shell pkg-config --libs   $(PKGS) 2>/dev/null)

CFLAGS  += $(PKG_CFLAGS)
LDLIBS  := $(PKG_LIBS)

OBJS := merlin-watcher.o decode.o

merlin-watcher: $(OBJS)
	$(CC) $(CFLAGS) -o $@ $(OBJS) $(LDLIBS)

%.o: %.c merlin_watcher.h
	$(CC) $(CFLAGS) -c -o $@ $<

.PHONY: clean check-deps
clean:
	rm -f merlin-watcher $(OBJS)

check-deps:
	@pkg-config --exists $(PKGS) || \
	  ( echo "*** install libnl-genl-3-dev (libnl3-devel on RPM)"; exit 1 )
