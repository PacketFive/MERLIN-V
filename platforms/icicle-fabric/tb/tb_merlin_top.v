// SPDX-License-Identifier: GPL-2.0-or-later
//
// tb/tb_merlin_top.v — end-to-end testbench for the fabric core.
//
// Drives the AXI-Lite slave directly (no AXI master IP needed for
// the smoke test).  Pushes the worked-example classifier image into
// IMEM, a synthetic ETH/IPv4 frame into DMEM, releases the core, and
// asserts that the EXIT register reads 2 (PASS).  Repeats with an
// ARP-like frame and asserts EXIT==1 (DROP).
//
// Run:    cd ../tb && make
// Expects: "[tb] PASS" twice + "[tb] all good" at end.

`timescale 1ns/1ps
`default_nettype none

module tb_merlin_top;

	reg              clk = 1'b0;
	reg              rst_n;
	always #5 clk = ~clk;   // 100 MHz

	// AXI-Lite signals into merlin_top
	reg  [11:0] awaddr;
	reg         awvalid;
	wire        awready;
	reg  [31:0] wdata;
	reg  [3:0]  wstrb;
	reg         wvalid;
	wire        wready;
	wire [1:0]  bresp;
	wire        bvalid;
	reg         bready;
	reg  [11:0] araddr;
	reg         arvalid;
	wire        arready;
	wire [31:0] rdata;
	wire [1:0]  rresp;
	wire        rvalid;
	reg         rready;

	merlin_top u_dut (
		.clk       (clk),
		.rst_n     (rst_n),
		.s_awaddr  (awaddr),  .s_awvalid (awvalid), .s_awready (awready),
		.s_wdata   (wdata),   .s_wstrb   (wstrb),   .s_wvalid  (wvalid),  .s_wready (wready),
		.s_bresp   (bresp),   .s_bvalid  (bvalid),  .s_bready  (bready),
		.s_araddr  (araddr),  .s_arvalid (arvalid), .s_arready (arready),
		.s_rdata   (rdata),   .s_rresp   (rresp),   .s_rvalid  (rvalid), .s_rready  (rready)
	);

	// ----------------------------------------------------------------
	// AXI-Lite helpers
	// ----------------------------------------------------------------
	task axi_write(input [11:0] addr, input [31:0] data);
		begin
			@(posedge clk);
			awaddr  <= addr;  awvalid <= 1'b1;
			wdata   <= data;  wstrb   <= 4'hF;  wvalid <= 1'b1;
			bready  <= 1'b1;
			@(posedge clk);
			while (!(awready && wready)) @(posedge clk);
			awvalid <= 1'b0; wvalid <= 1'b0;
			while (!bvalid) @(posedge clk);
			@(posedge clk);
			bready  <= 1'b0;
		end
	endtask

	task axi_read(input [11:0] addr, output [31:0] data);
		begin
			@(posedge clk);
			araddr  <= addr; arvalid <= 1'b1;
			rready  <= 1'b1;
			@(posedge clk);
			while (!arready) @(posedge clk);
			arvalid <= 1'b0;
			while (!rvalid) @(posedge clk);
			data    = rdata;
			@(posedge clk);
			rready  <= 1'b0;
		end
	endtask

	// ----------------------------------------------------------------
	// Worked-example classifier (7 instructions, 28 bytes)
	// ----------------------------------------------------------------
	// Index:    insn      | mnemonic
	//   0:  0x00c54583    | lbu  a1, 12(a0)
	//   1:  0x00800613    | addi a2, x0, 8
	//   2:  0x00c58463    | beq  a1, a2, +8  (forward to .pass)
	//   3:  0x00100513    | addi a0, x0, 1   (DROP)
	//   4:  0x00008067    | jalr x0, ra, 0
	//   5:  0x00200513    | addi a0, x0, 2   (PASS)
	//   6:  0x00008067    | jalr x0, ra, 0
	reg [31:0] classifier_text [0:6];
	initial begin
		classifier_text[0] = 32'h00c54583;
		classifier_text[1] = 32'h00800613;
		classifier_text[2] = 32'h00c58463;
		classifier_text[3] = 32'h00100513;
		classifier_text[4] = 32'h00008067;
		classifier_text[5] = 32'h00200513;
		classifier_text[6] = 32'h00008067;
	end

	task upload_program;
		integer i;
		begin
			axi_write(12'h010, 32'h0);             // IMEM_WSEL = 0
			for (i = 0; i < 7; i = i + 1)
				axi_write(12'h014, classifier_text[i]);
		end
	endtask

	// Pack a 16-byte packet header (first 16 bytes of ETH frame) into
	// DMEM, four 32-bit words.  Little-endian byte order.
	task upload_pkt(input [7:0] et_hi, input [7:0] et_lo);
		begin
			axi_write(12'h018, 32'h0);             // DMEM_WSEL = 0
			// bytes 0..3: dst MAC bytes 0..3
			axi_write(12'h01c, 32'hffffffff);
			// bytes 4..7: dst MAC 4..5 + src MAC 0..1
			axi_write(12'h01c, 32'h0002ffff);
			// bytes 8..11: src MAC 2..5
			axi_write(12'h01c, 32'h01000000);
			// bytes 12..15: EtherType + 2 payload bytes
			// byte 12 = et_hi (high byte of EtherType)
			// byte 13 = et_lo
			axi_write(12'h01c, {16'h0000, et_lo, et_hi});
		end
	endtask

	task run_and_check(input [31:0] expected, input [127:0] label);
		integer cycles;
		reg [31:0] status;
		reg [31:0] exit_v;
		begin
			// Soft reset
			axi_write(12'h000, 32'h2);
			repeat (4) @(posedge clk);
			axi_write(12'h000, 32'h0);
			// Re-upload program
			upload_program();
			// Start
			axi_write(12'h000, 32'h1);
			// Poll halted
			cycles = 0;
			status = 0;
			while (status[0] == 1'b0 && cycles < 200) begin
				axi_read(12'h004, status);
				cycles = cycles + 1;
			end
			axi_read(12'h008, exit_v);
			if (exit_v === expected)
				$display("[tb] %0s: exit=%0d (PASS, expected %0d, cycles=%0d)",
					 label, exit_v, expected, cycles);
			else begin
				$display("[tb] %0s: exit=%0d (FAIL, expected %0d)",
					 label, exit_v, expected);
				$fatal(1);
			end
		end
	endtask

	// ----------------------------------------------------------------
	// Main
	// ----------------------------------------------------------------
	initial begin
		// Reset
		rst_n = 1'b0;
		awvalid = 1'b0; wvalid = 1'b0; bready = 1'b0;
		arvalid = 1'b0; rready = 1'b0;
		repeat (4) @(posedge clk);
		rst_n = 1'b1;
		@(posedge clk);

		// Case 1: IPv4 packet (byte 12 == 0x08) → expect PASS=2
		upload_pkt(8'h08, 8'h00);
		run_and_check(32'd2, "ETH/IPv4");

		// Case 2: RARP-like packet (byte 12 == 0x80) → expect DROP=1
		// Note: upload_pkt re-uploads from DMEM_WSEL=0, overwriting.
		upload_pkt(8'h80, 8'h35);
		run_and_check(32'd1, "ETH/RARP");

		$display("[tb] all good");
		$finish;
	end

	// Safety: bound the simulation
	initial begin
		#100000;
		$display("[tb] TIMEOUT");
		$fatal(1);
	end

endmodule
