// SPDX-License-Identifier: GPL-2.0-or-later
//
// rtl/merlin_top.v — top-level for the PolarFire fabric demonstrator.
//
//   ┌────────────────────────────────────────────────────────────┐
//   │  AXI-Lite slave (host CPU drives)                          │
//   │                                                            │
//   │  axi_ctrl     ──IMEM write port──▶  merlin_imem  ─▶ core   │
//   │     │                                                      │
//   │     ├──DMEM write port──▶  merlin_dmem  ◀──▶ core data bus │
//   │     │                                                      │
//   │     └──run/reset────────▶  merlin_core (RV32 soft core)    │
//   │     ◀──halted/exit_value──                                 │
//   └────────────────────────────────────────────────────────────┘
//
// Default parameters give 1 KiB IMEM and 1 KiB DMEM, enough for the
// 28-byte worked-example classifier with plenty of headroom.

`default_nettype none

module merlin_top #(
	parameter integer AXI_ADDR_W = 12,
	parameter integer AXI_DATA_W = 32
) (
	input  wire                  clk,
	input  wire                  rst_n,

	// AXI-Lite slave (from the host CPU's AXI-Lite master)
	input  wire [AXI_ADDR_W-1:0] s_awaddr,
	input  wire                  s_awvalid,
	output wire                  s_awready,
	input  wire [AXI_DATA_W-1:0] s_wdata,
	input  wire [3:0]            s_wstrb,
	input  wire                  s_wvalid,
	output wire                  s_wready,
	output wire [1:0]            s_bresp,
	output wire                  s_bvalid,
	input  wire                  s_bready,
	input  wire [AXI_ADDR_W-1:0] s_araddr,
	input  wire                  s_arvalid,
	output wire                  s_arready,
	output wire [AXI_DATA_W-1:0] s_rdata,
	output wire [1:0]            s_rresp,
	output wire                  s_rvalid,
	input  wire                  s_rready
);

	// -------------------------------------------------------------
	// Internal wires
	// -------------------------------------------------------------
	wire        core_run;
	wire        core_rst_n;
	wire        core_halted;
	wire [31:0] core_exit_value;

	// core_running is a synthetic status bit derived from core_run + halted.
	wire        core_running = core_run & ~core_halted;

	wire        imem_we;
	wire [11:0] imem_waddr;
	wire [31:0] imem_wdata;

	wire        dmem_we_host;
	wire [11:0] dmem_waddr;
	wire [31:0] dmem_wdata_host;

	wire [31:0] imem_addr_core;
	wire [31:0] imem_rdata;

	wire [31:0] dmem_addr_core;
	wire [31:0] dmem_wdata_core;
	wire [3:0]  dmem_wstrb_core;
	wire        dmem_we_core;
	wire [31:0] dmem_rdata;

	// -------------------------------------------------------------
	// AXI-Lite control block
	// -------------------------------------------------------------
	axi_ctrl #(
		.ADDR_W (AXI_ADDR_W),
		.DATA_W (AXI_DATA_W)
	) u_axi_ctrl (
		.clk             (clk),
		.rst_n           (rst_n),
		.s_awaddr        (s_awaddr),
		.s_awvalid       (s_awvalid),
		.s_awready       (s_awready),
		.s_wdata         (s_wdata),
		.s_wstrb         (s_wstrb),
		.s_wvalid        (s_wvalid),
		.s_wready        (s_wready),
		.s_bresp         (s_bresp),
		.s_bvalid        (s_bvalid),
		.s_bready        (s_bready),
		.s_araddr        (s_araddr),
		.s_arvalid       (s_arvalid),
		.s_arready       (s_arready),
		.s_rdata         (s_rdata),
		.s_rresp         (s_rresp),
		.s_rvalid        (s_rvalid),
		.s_rready        (s_rready),
		.core_run        (core_run),
		.core_rst_n      (core_rst_n),
		.core_halted     (core_halted),
		.core_running    (core_running),
		.core_exit_value (core_exit_value),
		.imem_we         (imem_we),
		.imem_waddr      (imem_waddr),
		.imem_wdata      (imem_wdata),
		.dmem_we         (dmem_we_host),
		.dmem_waddr      (dmem_waddr),
		.dmem_wdata      (dmem_wdata_host)
	);

	// Combined core reset: external rst_n AND software reset bit.
	wire core_rst_n_eff = rst_n & core_rst_n;

	// -------------------------------------------------------------
	// Memories
	// -------------------------------------------------------------
	merlin_imem #(.WORDS(256), .ADDR_W(12)) u_imem (
		.clk    (clk),
		.r_addr (imem_addr_core[11:0]),
		.r_data (imem_rdata),
		.w_en   (imem_we),
		.w_addr (imem_waddr),
		.w_data (imem_wdata)
	);

	merlin_dmem #(.WORDS(256), .ADDR_W(12)) u_dmem (
		.clk     (clk),
		.c_addr  (dmem_addr_core[11:0]),
		.c_wdata (dmem_wdata_core),
		.c_wstrb (dmem_wstrb_core),
		.c_we    (dmem_we_core),
		.c_rdata (dmem_rdata),
		.h_we    (dmem_we_host),
		.h_addr  (dmem_waddr),
		.h_wdata (dmem_wdata_host)
	);

	// -------------------------------------------------------------
	// Soft core (stub model; replace for synthesis)
	// -------------------------------------------------------------
	merlin_core u_core (
		.clk        (clk),
		.rst_n      (core_rst_n_eff),
		.run        (core_run),
		.imem_addr  (imem_addr_core),
		.imem_rdata (imem_rdata),
		.dmem_addr  (dmem_addr_core),
		.dmem_wdata (dmem_wdata_core),
		.dmem_wstrb (dmem_wstrb_core),
		.dmem_we    (dmem_we_core),
		.dmem_rdata (dmem_rdata),
		.halted     (core_halted),
		.exit_value (core_exit_value)
	);

endmodule
