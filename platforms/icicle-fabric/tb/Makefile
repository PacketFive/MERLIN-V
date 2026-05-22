# SPDX-License-Identifier: GPL-2.0-or-later
#
# tb/Makefile — iverilog smoke testbench

IVERILOG ?= iverilog
VVP      ?= vvp

RTL := \
	../rtl/merlin_top.v \
	../rtl/axi_ctrl.v \
	../rtl/merlin_imem.v \
	../rtl/merlin_dmem.v \
	../rtl/merlin_core.v

.PHONY: all clean
all: run

tb_merlin_top.vvp: tb_merlin_top.v $(RTL)
	$(IVERILOG) -g2012 -o $@ $(RTL) tb_merlin_top.v

run: tb_merlin_top.vvp
	$(VVP) -n $<

clean:
	rm -f *.vvp *.vcd
