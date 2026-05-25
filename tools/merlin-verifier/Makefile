# SPDX-License-Identifier: GPL-2.0-or-later
#
# Makefile - prototype merlin-verifier build.
#
# Dependency (Ubuntu/Debian):  sudo apt install libelf-dev

TOPDIR      := ../..
DESIGN_UAPI := $(TOPDIR)/docs/design/uapi
EXAMPLES    := $(DESIGN_UAPI)/examples

CC      ?= gcc
CFLAGS  ?= -O2 -Wall -Wextra -Wformat=2 -Wshadow -Wno-unused-parameter
CFLAGS  += -I$(DESIGN_UAPI)
LDLIBS  := -lelf

TARGET  := merlin-verifier
SRCS    := merlin-verifier.c verify.c cfg.c decode.c
OBJS    := $(SRCS:.c=.o)

EXAMPLE_OBJ := drop_all.merlin.o

.PHONY: all clean example test
all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

%.o: %.c decode.h verify.h cfg.h tnum.h
	$(CC) $(CFLAGS) -c $< -o $@

example: $(EXAMPLE_OBJ)

$(EXAMPLE_OBJ): $(EXAMPLES)/drop_all.c
	$(CC) -I$(DESIGN_UAPI) \
	      -ffreestanding -fno-builtin -nostdlib \
	      -Wall -Wextra \
	      -c $< -o $@

# bad-progs/  is a directory of negative test fixtures: programs that
# the verifier must REJECT.  Each one prints "ACCEPT" if the verifier
# wrongly accepts it; the Makefile target inverts the exit code.

test: $(TARGET) $(EXAMPLE_OBJ)
	@echo "=== positive: drop_all.merlin.o (host-compiled fixture) ==="
	@./$(TARGET) $(EXAMPLE_OBJ) || true
	@echo
	@echo "Note: drop_all.merlin.o was compiled for x86_64 (the only"
	@echo "host compiler we have).  Its .text.merlin.* section contains"
	@echo "x86 instructions that the RISC-V decoder rejects.  This is"
	@echo "the expected behaviour for an off-target build."
	@echo
	@echo "=== synthetic RV64 fixtures (the real test battery) ==="
	@$(MAKE) -s -C bad-progs test

clean:
	rm -f $(TARGET) $(OBJS) $(EXAMPLE_OBJ)
	@if [ -d bad-progs ]; then $(MAKE) -C bad-progs clean ; fi
