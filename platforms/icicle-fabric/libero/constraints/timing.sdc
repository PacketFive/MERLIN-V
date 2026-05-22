## SPDX-License-Identifier: GPL-2.0-or-later
##
## libero/constraints/timing.sdc — Synopsys Design Constraints.
##
## The fabric clock the MSS-to-fabric bridge presents is fixed by
## the MSS Configurator (default 150 MHz on FIC0).  Constrain only
## the input clock here; the slack against 150 MHz should be
## comfortable for the design as written (single-cycle ALU paths,
## no multi-cycle critical paths).

# Primary clock: assume 150 MHz FIC0 fabric clock comes through
# the MSS bridge as `clk`.  Period = 6.667 ns.
create_clock -name CLK -period 6.667 [get_ports clk]

# Reset is an asynchronous signal; let the tool insert
# synchronizers if needed.
set_false_path -through [get_ports rst_n]

# AXI-Lite handshake signals are part of the synchronous bus and
# do not need special constraints.
