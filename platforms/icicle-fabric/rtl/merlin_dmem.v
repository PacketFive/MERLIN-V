// SPDX-License-Identifier: GPL-2.0-or-later
//
// rtl/merlin_dmem.v — data scratchpad RAM.
//
// 256 words × 32 bits = 1 KiB.  Used for:
//   * The packet ctx buffer that the host pushes in before run.
//   * Any scratch storage the program needs.
//
// Byte-write strobes on the core port so SB/SH/SW work.

`default_nettype none

module merlin_dmem #(
	parameter integer WORDS = 256,
	parameter integer ADDR_W = 10
) (
	input  wire              clk,

	// Core port (read+write)
	input  wire [ADDR_W-1:0] c_addr,
	input  wire [31:0]       c_wdata,
	input  wire [3:0]        c_wstrb,
	input  wire              c_we,
	output reg  [31:0]       c_rdata,

	// Host port (write-only on this simple design)
	input  wire              h_we,
	input  wire [ADDR_W-1:0] h_addr,
	input  wire [31:0]       h_wdata
);

	reg [31:0] mem [0:WORDS-1];
	integer i;

	initial begin
		for (i = 0; i < WORDS; i = i + 1)
			mem[i] = 32'h0;
	end

	always @(posedge clk) begin
		// Host write has priority over core (host runs while core
		// is held in reset).
		if (h_we)
			mem[h_addr >> 2] <= h_wdata;
		else if (c_we) begin
			if (c_wstrb[0]) mem[c_addr >> 2][ 7: 0] <= c_wdata[ 7: 0];
			if (c_wstrb[1]) mem[c_addr >> 2][15: 8] <= c_wdata[15: 8];
			if (c_wstrb[2]) mem[c_addr >> 2][23:16] <= c_wdata[23:16];
			if (c_wstrb[3]) mem[c_addr >> 2][31:24] <= c_wdata[31:24];
		end
		c_rdata <= mem[c_addr >> 2];
	end

endmodule
