# SPDX-License-Identifier: GPL-2.0-or-later
#
# Makefile - prototype merlin-objtool build.
#
# Targets:
#   make            -> build merlin-objtool
#   make clean      -> remove build artefacts
#   make example    -> compile docs/design/uapi/examples/drop_all.c
#                      (the canonical test fixture for the tool)
#   make test       -> build the example and run a smoke test
#
# Dependencies (Ubuntu/Debian):
#   sudo apt install libelf-dev libssl-dev

TOPDIR     := ../..
DESIGN_UAPI := $(TOPDIR)/docs/design/uapi
EXAMPLES   := $(DESIGN_UAPI)/examples

CC      ?= gcc
CFLAGS  ?= -O2 -Wall -Wextra -Wformat=2 -Wshadow -Wno-unused-parameter
CFLAGS  += -I$(DESIGN_UAPI)
LDLIBS  := -lelf -lcrypto

OBJTOOL := merlin-objtool

.PHONY: all clean example test
all: $(OBJTOOL)

$(OBJTOOL): $(OBJTOOL).c
	$(CC) $(CFLAGS) -o $@ $< $(LDLIBS)

EXAMPLE_OBJ := drop_all.merlin.o

# Build the canonical test fixture - the drop_all program from the
# design tree.  We compile it for the host (off-target) because the
# headers were designed to work for compile-test in this mode; the
# program is not runnable but its sections, structs, and symbols
# are exactly the same as on a real RV64 build.
example: $(EXAMPLE_OBJ)

$(EXAMPLE_OBJ): $(EXAMPLES)/drop_all.c
	$(CC) -I$(DESIGN_UAPI) \
	      -ffreestanding -fno-builtin -nostdlib \
	      -Wall -Wextra \
	      -c $< -o $@

test: $(OBJTOOL) $(EXAMPLE_OBJ)
	@echo "=== info ==="
	./$(OBJTOOL) info $(EXAMPLE_OBJ)
	@echo
	@echo "=== dump-meta ==="
	./$(OBJTOOL) dump-meta $(EXAMPLE_OBJ)
	@echo
	@echo "=== dump-maps ==="
	./$(OBJTOOL) dump-maps $(EXAMPLE_OBJ)
	@echo
	@echo "=== sha256 ==="
	./$(OBJTOOL) sha256 $(EXAMPLE_OBJ)
	@echo
	@echo "=== validate (strict; expected to FAIL on host-built fixture) ==="
	@./$(OBJTOOL) validate $(EXAMPLE_OBJ) ; \
		rc=$$? ; \
		if [ "$$rc" = "1" ]; then \
			echo "(expected non-zero exit on x86 host build; test pass)" ; \
		else \
			echo "UNEXPECTED rc=$$rc" ; exit 1 ; \
		fi

clean:
	rm -f $(OBJTOOL) $(EXAMPLE_OBJ) *.o
