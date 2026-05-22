// SPDX-License-Identifier: GPL-2.0-or-later
//
// rtl/merlin_imem.v — instruction RAM (single-port BRAM model).
//
// 256 words × 32 bits = 1 KiB.  Plenty for the worked-example
// classifier (28 bytes) plus headroom.  Synthesisable as a single
// PolarFire LSRAM block; the simulation model is a plain reg array.
//
// Writable from the AXI-Lite controller (image upload).  Read-only
// from the core's instruction bus.

`default_nettype none

module merlin_imem #(
	parameter integer WORDS = 256,
	parameter integer ADDR_W = 10        // log2(WORDS*4)
) (
	input  wire             clk,

	// Core read port
	input  wire [ADDR_W-1:0] r_addr,     // byte address
	output reg  [31:0]       r_data,

	// Host write port (from AXI-Lite controller)
	input  wire              w_en,
	input  wire [ADDR_W-1:0] w_addr,
	input  wire [31:0]       w_data
);

	reg [31:0] mem [0:WORDS-1];
	integer i;

	initial begin
		for (i = 0; i < WORDS; i = i + 1)
			mem[i] = 32'h0;
	end

	always @(posedge clk) begin
		if (w_en)
			mem[w_addr >> 2] <= w_data;
		r_data <= mem[r_addr >> 2];
	end

endmodule
