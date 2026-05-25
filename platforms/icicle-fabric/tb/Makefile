# SPDX-License-Identifier: GPL-2.0-or-later
#
# tb/Makefile — iverilog smoke testbench
#
# Two cores supported:
#   make             — build + run with the simulation stub
#                      (rtl/merlin_core.v), the original tb.
#   make picorv32    — build + run with PicoRV32 vendored at
#                      rtl/vendor/picorv32/picorv32.v, wrapped by
#                      rtl/merlin_core_picorv32.v.  Uses the
#                      shim-bearing tb tb_merlin_top_picorv32.v.

IVERILOG ?= iverilog
VVP      ?= vvp

# Stub core (default)
RTL_STUB := \
	../rtl/merlin_top.v \
	../rtl/axi_ctrl.v \
	../rtl/merlin_imem.v \
	../rtl/merlin_dmem.v \
	../rtl/merlin_core.v

# PicoRV32 core
RTL_PICO := \
	../rtl/merlin_top.v \
	../rtl/axi_ctrl.v \
	../rtl/merlin_imem.v \
	../rtl/merlin_dmem.v \
	../rtl/merlin_core_picorv32.v \
	../rtl/vendor/picorv32/picorv32.v

.PHONY: all clean run picorv32 picorv32-build
all: run

tb_merlin_top.vvp: tb_merlin_top.v $(RTL_STUB)
	$(IVERILOG) -g2012 -o $@ $(RTL_STUB) tb_merlin_top.v

run: tb_merlin_top.vvp
	$(VVP) -n $<

tb_merlin_top_picorv32.vvp: tb_merlin_top_picorv32.v $(RTL_PICO)
	$(IVERILOG) -g2012 -o $@ $(RTL_PICO) tb_merlin_top_picorv32.v

picorv32: tb_merlin_top_picorv32.vvp
	$(VVP) -n $<

picorv32-build: tb_merlin_top_picorv32.vvp

clean:
	rm -f *.vvp *.vcd
