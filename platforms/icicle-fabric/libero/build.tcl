# SPDX-License-Identifier: GPL-2.0-or-later
#
# libero/build.tcl — headless Microchip Libero SoC build script.
#
# Usage (in the Libero SoC TCL shell):
#
#   libero SCRIPT:build.tcl
#
# Or from the command line (Linux/Windows):
#
#   /opt/microchip/Libero_SoC/bin/libero SCRIPT:build.tcl
#
# What this does:
#   1. Create a project for the MPFS Icicle Kit's PolarFire die.
#   2. Add all rtl/*.v sources.
#   3. Add the pin-map and timing constraint files.
#   4. Run synthesis (Synplify Pro for Libero), place-and-route,
#      verify timing, and generate the bitstream.
#
# Output: build/merlin_fabric.bit (or .stp, depending on Libero
# version).
#
# Note: replace the body of rtl/merlin_core.v with a real soft-core
# (VexRiscv / PicoRV32 / MiV) before running this script, or the
# tool will refuse to synthesise the stub-only model meant for sim.

# -- Project parameters ---------------------------------------------
set PROJECT_NAME    "merlin_fabric"
set PROJECT_DIR     "[file dirname [info script]]/../build/libero"
set PART_NUMBER     "MPFS250T_FCVG484"
set DIE_FAMILY      "PolarFireSoC"

# -- 1. Create project ----------------------------------------------
new_project \
    -name        $PROJECT_NAME \
    -location    $PROJECT_DIR \
    -family      $DIE_FAMILY \
    -die         $PART_NUMBER \
    -package     "FCVG484" \
    -speed       "STD" \
    -hdl         "VERILOG"

# -- 2. Add RTL sources ---------------------------------------------
set RTL_DIR "[file dirname [info script]]/../rtl"
create_links -hdl_source "$RTL_DIR/merlin_top.v"
create_links -hdl_source "$RTL_DIR/axi_ctrl.v"
create_links -hdl_source "$RTL_DIR/merlin_imem.v"
create_links -hdl_source "$RTL_DIR/merlin_dmem.v"
create_links -hdl_source "$RTL_DIR/merlin_core.v"

build_design_hierarchy
set_root -module merlin_top

# -- 3. Add constraints ----------------------------------------------
set CONSTR_DIR "[file dirname [info script]]/constraints"
create_links -io_pdc      "$CONSTR_DIR/pinmap.pdc"
create_links -sdc         "$CONSTR_DIR/timing.sdc"

organize_tool_files \
    -tool {SYNTHESIZE}            -file "$CONSTR_DIR/timing.sdc"
organize_tool_files \
    -tool {PLACEROUTE}            -file "$CONSTR_DIR/pinmap.pdc"
organize_tool_files \
    -tool {PLACEROUTE}            -file "$CONSTR_DIR/timing.sdc"
organize_tool_files \
    -tool {VERIFYTIMING}          -file "$CONSTR_DIR/timing.sdc"

# -- 4. Run the flow -------------------------------------------------
run_tool -name {SYNTHESIZE}
run_tool -name {PLACEROUTE}
run_tool -name {VERIFYTIMING}
run_tool -name {GENERATEPROGRAMMINGDATA}
run_tool -name {GENERATEPROGRAMMINGFILE}

save_project
close_project

puts "==> Bitstream at: $PROJECT_DIR/designer/$PROJECT_NAME/$PROJECT_NAME.bit"
