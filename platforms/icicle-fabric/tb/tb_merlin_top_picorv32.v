// SPDX-License-Identifier: GPL-2.0-or-later
//
// tb/tb_merlin_top_picorv32.v - end-to-end testbench against the real
// PicoRV32-based core (merlin_core_picorv32.v).
//
// Differences vs tb_merlin_top.v (the stub-targeted tb):
//
//   1. The program in IMEM is wrapped in a short shim because real
//      RISC-V has no implicit "halt" instruction:
//
//        _start:
//            lui  a0, 0x10000          ; ctx ptr (DMEM base)
//            jal  ra, .classifier
//            lui  t0, 0x20000          ; MMIO halt addr
//            sw   a0, 0(t0)            ; halt + latch exit value
//        .spin:
//            jal  x0, .spin
//        .classifier:
//            lbu  a1, 12(a0)
//            addi a2, x0, 8
//            beq  a1, a2, .pass
//            addi a0, x0, 1            ; DROP
//            jalr x0, ra, 0
//        .pass:
//            addi a0, x0, 2            ; PASS
//            jalr x0, ra, 0
//
//   2. The classifier's BEQ uses offset +12 (not +8 as in the
//      stub-era worked example) so that on a real CPU the branch
//      lands at the PASS block.  The stub tolerated the older +8
//      offset only because its BRAM-latency handling caused each
//      instruction to execute twice; PicoRV32 does not have that
//      bug, so the bytecode needs the correct offset.  The thesis
//      worked-example bytes are the same instructions in the same
//      order — only the immediate of the conditional branch
//      differs.
//
//   3. The wrapper exposes the same external port list as the stub
//      (rtl/merlin_core.v), so merlin_top.v is unchanged.

`timescale 1ns / 1ps
`default_nettype none

module tb_merlin_top_picorv32;

	reg clk = 1'b0;
	reg rst_n;
	always
	#5 clk = ~clk; // 100 MHz

	// AXI-Lite signals into merlin_top
	reg [11 : 0] awaddr;
	reg awvalid;
	wire awready;
	reg [31 : 0] wdata;
	reg [3 : 0] wstrb;
	reg wvalid;
	wire wready;
	wire [1 : 0] bresp;
	wire bvalid;
	reg bready;
	reg [11 : 0] araddr;
	reg arvalid;
	wire arready;
	wire [31 : 0] rdata;
	wire [1 : 0] rresp;
	wire rvalid;
	reg rready;

	merlin_top u_dut(.clk(clk),
	.rst_n(rst_n),
	.s_awaddr(awaddr),
	.s_awvalid(awvalid),
	.s_awready(awready),
	.s_wdata(wdata),
	.s_wstrb(wstrb),
	.s_wvalid(wvalid),
	.s_wready(wready),
	.s_bresp(bresp),
	.s_bvalid(bvalid),
	.s_bready(bready),
	.s_araddr(araddr),
	.s_arvalid(arvalid),
	.s_arready(arready),
	.s_rdata(rdata),
	.s_rresp(rresp),
	.s_rvalid(rvalid),
	.s_rready(rready));

	// ----------------------------------------------------------------
	// AXI-Lite helpers
	// ----------------------------------------------------------------
	task axi_write
	(input [11 : 0] addr,
	input [31 : 0] data);
	begin
		@(posedge clk);
		awaddr <= addr;
		awvalid <= 1'b1;
		wdata <= data;
		wstrb <= 4'hF;
		wvalid <= 1'b1;
		bready <= 1'b1;
		@(posedge clk);
		while (!(awready && wready)) @(posedge clk)
		;
		awvalid <= 1'b0;
		wvalid <= 1'b0;
		while (!bvalid) @(posedge clk)
		;
		@(posedge clk);
		bready <= 1'b0;
	end
endtask

	task axi_read
	(input [11 : 0] addr,
	output [31 : 0] data);
	begin
		@(posedge clk);
		araddr <= addr;
		arvalid <= 1'b1;
		rready <= 1'b1;
		@(posedge clk);
		while (!arready) @(posedge clk)
		;
		arvalid <= 1'b0;
		while (!rvalid) @(posedge clk)
		;
		data = rdata;
		@(posedge clk);
		rready <= 1'b0;
	end
endtask

	// ----------------------------------------------------------------
	// Program image: shim (5 insns) + classifier (7 insns) = 12 words
	// ----------------------------------------------------------------
	reg [31 : 0] program_img[0 : 11];
	initial begin
		program_img[0] =
		32'h10000537; // lui  a0, 0x10000   (a0 = DMEM base)
		program_img[1] =
		32'h010000ef; // jal  ra, +16       (-> .classifier)
		program_img[2] = 32'h200002b7; // lui  t0, 0x20000   (HALT addr)
		program_img[3] =
		32'h00a2a023; // sw   a0, 0(t0)     (write halt + value)
		program_img[4] = 32'h0000006f; // jal  x0, 0         (spin)
		program_img[5] = 32'h00c54583; // lbu  a1, 12(a0)    .classifier
		program_img[6] = 32'h00800613; // addi a2, x0, 8
		program_img[7] = 32'h00c58663; // beq  a1, a2, +12   (-> .pass)
		program_img[8] = 32'h00100513; // addi a0, x0, 1     (DROP)
		program_img[9] = 32'h00008067; // jalr x0, ra, 0
		program_img[10] = 32'h00200513; // addi a0, x0, 2     (PASS)
		program_img[11] = 32'h00008067; // jalr x0, ra, 0
	end

	task upload_program;
	integer i;
	begin
		axi_write(12'h010, 32'h0); // IMEM_WSEL = 0
		for (i = 0; i < 12; i = i + 1)
		axi_write(12'h014, program_img[i]);
	end
endtask

	// Pack the first 16 bytes of the ETH frame into DMEM as four
	// 32-bit little-endian words.  byte 12 is the EtherType high byte.
	task upload_pkt
	(input [7 : 0] et_hi,
	input [7 : 0] et_lo);
	begin
		axi_write(12'h018, 32'h0); // DMEM_WSEL = 0
		axi_write(12'h01c, 32'hffffffff); // bytes 0..3
		axi_write(12'h01c, 32'h0002ffff); // bytes 4..7
		axi_write(12'h01c, 32'h01000000); // bytes 8..11
		axi_write(12'h01c,
		{ 16'h0000, et_lo, et_hi }); // 12..15
	end
endtask

	task run_and_check
	(input [31 : 0] expected,
	input [127 : 0] label);
	integer cycles;
	reg [31 : 0] status;
	reg [31 : 0] exit_v;
	begin
		// Soft reset
		axi_write(12'h000, 32'h2);
		repeat (4) @(posedge clk)
		;
		axi_write(12'h000, 32'h0);
		// Re-upload program (the soft reset clears core state
		// but not IMEM; uploading again is cheap and explicit)
		upload_program();
		// Start
		axi_write(12'h000, 32'h1);
		// Poll halted.  PicoRV32 needs ~80-120 cycles to chew
		// through the 12-instruction shim+classifier (it spends
		// 5 cycles per insn on the unified bus path), so allow
		// generously.
		cycles = 0;
		status = 0;
		while (status[0] == 1'b0 && cycles < 1000) begin
			axi_read(12'h004, status);
			cycles = cycles + 1;
		end
		axi_read(12'h008, exit_v);
		if (exit_v === expected)
		$display(
		"[tb] %0s: exit=%0d (PASS, expected %0d, polls=%0d)",
		label, exit_v, expected, cycles);
		else begin
			$display(
			"[tb] %0s: exit=%0d (FAIL, expected %0d, status=%h)",
			label, exit_v, expected, status);
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
		awvalid = 1'b0;
		wvalid = 1'b0;
		bready = 1'b0;
		arvalid = 1'b0;
		rready = 1'b0;
		repeat (4) @(posedge clk)
		;
		rst_n = 1'b1;
		@(posedge clk);

		// Case 1: IPv4 packet (byte 12 == 0x08) → expect PASS=2
		upload_pkt(8'h08, 8'h00);
		run_and_check(32'd2, "ETH/IPv4");

		// Case 2: RARP-like packet (byte 12 == 0x80) → expect DROP=1
		upload_pkt(8'h80, 8'h35);
		run_and_check(32'd1, "ETH/RARP");

		$display("[tb] all good (picorv32 core)");
		$finish;
	end

	// Safety: bound the simulation
	initial begin
		#1000000;
		$display("[tb] TIMEOUT");
		$fatal(1);
	end
endmodule
