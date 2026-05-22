// SPDX-License-Identifier: GPL-2.0-or-later
//
// rtl/axi_ctrl.v — AXI-Lite slave: control registers + image upload.
//
// One-cycle handshake design: when awvalid && wvalid are both high,
// the slave asserts awready and wready simultaneously, performs the
// write, and asserts bvalid the next cycle.  This matches the
// AXI-Lite spec (channels are independent but the slave is allowed
// to wait until both are valid before completing either).
//
// Memory map (byte offsets from the AXI-Lite slave base):
//
//   0x000  CTRL       wo  bit0 = run, bit1 = soft_reset (active high)
//   0x004  STATUS     ro  bit0 = halted, bit1 = running
//   0x008  EXIT       ro  exit_value (a0 at halt)
//   0x010  IMEM_WSEL  rw  word index for next IMEM_WDATA write
//   0x014  IMEM_WDATA wo  write one 32-bit insn; WSEL auto-increments
//   0x018  DMEM_WSEL  rw  word index for next DMEM_WDATA write
//   0x01c  DMEM_WDATA wo  write one 32-bit data word; WSEL auto-inc
//
// Synthesisable as-is on PolarFire fabric.

`default_nettype none

module axi_ctrl #(
	parameter integer ADDR_W = 12,
	parameter integer DATA_W = 32
) (
	input  wire                clk,
	input  wire                rst_n,

	// AXI-Lite slave
	input  wire [ADDR_W-1:0]   s_awaddr,
	input  wire                s_awvalid,
	output wire                s_awready,
	input  wire [DATA_W-1:0]   s_wdata,
	input  wire [DATA_W/8-1:0] s_wstrb,
	input  wire                s_wvalid,
	output wire                s_wready,
	output reg  [1:0]          s_bresp,
	output reg                 s_bvalid,
	input  wire                s_bready,
	input  wire [ADDR_W-1:0]   s_araddr,
	input  wire                s_arvalid,
	output reg                 s_arready,
	output reg  [DATA_W-1:0]   s_rdata,
	output reg  [1:0]          s_rresp,
	output reg                 s_rvalid,
	input  wire                s_rready,

	// Control / status to core
	output reg                 core_run,
	output reg                 core_rst_n,
	input  wire                core_halted,
	input  wire                core_running,
	input  wire [31:0]         core_exit_value,

	// IMEM write port
	output reg                 imem_we,
	output reg  [11:0]         imem_waddr,
	output reg  [31:0]         imem_wdata,

	// DMEM write port
	output reg                 dmem_we,
	output reg  [11:0]         dmem_waddr,
	output reg  [31:0]         dmem_wdata
);

	// -------------------------------------------------------------
	// Write channel — one-cycle handshake.
	//
	// awready and wready are asserted iff both awvalid and wvalid
	// are high and we are not currently waiting for bready.
	// -------------------------------------------------------------
	reg waiting_for_bready;

	wire write_fire = s_awvalid && s_wvalid && !waiting_for_bready;
	assign s_awready = write_fire;
	assign s_wready  = write_fire;

	reg [11:0] imem_wsel;
	reg [11:0] dmem_wsel;

	always @(posedge clk or negedge rst_n) begin
		if (!rst_n) begin
			s_bvalid           <= 1'b0;
			s_bresp            <= 2'b00;
			waiting_for_bready <= 1'b0;
			core_run           <= 1'b0;
			core_rst_n         <= 1'b1;
			imem_we            <= 1'b0;
			imem_waddr         <= 12'h0;
			imem_wdata         <= 32'h0;
			imem_wsel          <= 12'h0;
			dmem_we            <= 1'b0;
			dmem_waddr         <= 12'h0;
			dmem_wdata         <= 32'h0;
			dmem_wsel          <= 12'h0;
		end else begin
			imem_we <= 1'b0;
			dmem_we <= 1'b0;

			if (write_fire) begin
				case (s_awaddr[7:0])
				8'h00: begin
					core_run   <= s_wdata[0];
					core_rst_n <= ~s_wdata[1];
				end
				8'h10: imem_wsel <= s_wdata[11:0];
				8'h14: begin
					imem_we    <= 1'b1;
					imem_waddr <= imem_wsel << 2;
					imem_wdata <= s_wdata;
					imem_wsel  <= imem_wsel + 1'b1;
				end
				8'h18: dmem_wsel <= s_wdata[11:0];
				8'h1c: begin
					dmem_we    <= 1'b1;
					dmem_waddr <= dmem_wsel << 2;
					dmem_wdata <= s_wdata;
					dmem_wsel  <= dmem_wsel + 1'b1;
				end
				default: /* read-only registers ignore writes */ ;
				endcase
				s_bvalid           <= 1'b1;
				s_bresp            <= 2'b00;
				waiting_for_bready <= 1'b1;
			end

			if (s_bvalid && s_bready) begin
				s_bvalid           <= 1'b0;
				waiting_for_bready <= 1'b0;
			end
		end
	end

	// -------------------------------------------------------------
	// Read channel — one-cycle handshake.
	// -------------------------------------------------------------
	always @(posedge clk or negedge rst_n) begin
		if (!rst_n) begin
			s_arready <= 1'b1;
			s_rvalid  <= 1'b0;
			s_rdata   <= 32'h0;
			s_rresp   <= 2'b00;
		end else begin
			if (s_arvalid && s_arready) begin
				s_arready <= 1'b0;
				s_rvalid  <= 1'b1;
				s_rresp   <= 2'b00;
				case (s_araddr[7:0])
				8'h00: s_rdata <= {30'h0, ~core_rst_n, core_run};
				8'h04: s_rdata <= {30'h0, core_running, core_halted};
				8'h08: s_rdata <= core_exit_value;
				8'h10: s_rdata <= {20'h0, imem_wsel};
				8'h18: s_rdata <= {20'h0, dmem_wsel};
				default: s_rdata <= 32'hDEADC0DE;
				endcase
			end
			if (s_rvalid && s_rready) begin
				s_rvalid  <= 1'b0;
				s_arready <= 1'b1;
			end
		end
	end

endmodule
