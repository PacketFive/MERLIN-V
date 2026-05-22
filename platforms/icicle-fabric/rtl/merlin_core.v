// SPDX-License-Identifier: GPL-2.0-or-later
//
// rtl/merlin_core.v — vendor-neutral wrapper around a soft RV32 core.
//
// This file is the **simulation stub** used by tb/.  It implements a
// 3-state-per-instruction FSM that handles the 1-cycle BRAM read
// latency correctly:
//
//   S_FETCH:     drive imem_addr = pc;  go to S_DECODE.
//   S_DECODE:    imem_rdata is now valid; decode insn; execute
//                non-LOAD instructions.  For LOAD, drive dmem_addr
//                and go to S_LOAD_WAIT.
//   S_LOAD_WAIT: dmem_rdata is now valid; latch into rd; go to S_FETCH.
//
// Subset implemented (matches the worked-example classifier):
//   - ADDI   (opcode 0x13, funct3 0)
//   - LBU    (opcode 0x03, funct3 4)
//   - BEQ    (opcode 0x63, funct3 0)
//   - JALR   (opcode 0x67)
//
// On `jalr x0, ra, 0` with x[ra] == 0 we halt; the value in a0 (x10)
// is latched to `exit_value`.  Any other opcode → halt with sentinel
// 0xDEADBEEF.
//
// For Libero synthesis, replace this whole body with an instance of
// a real soft core (VexRiscv, PicoRV32, MiV) and wire its memory bus
// to the imem_/dmem_ ports declared below.  See rtl/README.md.

`default_nettype none

module merlin_core (
	input  wire        clk,
	input  wire        rst_n,
	input  wire        run,

	// Instruction bus (read-only)
	output reg  [31:0] imem_addr,
	input  wire [31:0] imem_rdata,

	// Data bus (byte-addressed)
	output reg  [31:0] dmem_addr,
	output reg  [31:0] dmem_wdata,
	output reg  [3:0]  dmem_wstrb,
	output reg         dmem_we,
	input  wire [31:0] dmem_rdata,

	output reg         halted,
	output reg  [31:0] exit_value
);

	localparam [1:0] S_FETCH     = 2'd0;
	localparam [1:0] S_DECODE    = 2'd1;
	localparam [1:0] S_LOAD_WAIT = 2'd2;

	reg [1:0]  state;
	reg [31:0] pc;
	reg [31:0] xreg [0:31];

	reg [4:0]  d_rd;

	// Combinational decode of imem_rdata (valid in S_DECODE)
	wire [31:0] insn   = imem_rdata;
	wire [6:0]  opcode = insn[6:0];
	wire [4:0]  rd     = insn[11:7];
	wire [2:0]  f3     = insn[14:12];
	wire [4:0]  rs1    = insn[19:15];
	wire [4:0]  rs2    = insn[24:20];
	wire signed [11:0] imm_i = insn[31:20];
	wire signed [12:0] imm_b = {insn[31], insn[7], insn[30:25], insn[11:8], 1'b0};

	wire [31:0] rs1_val = xreg[rs1];
	wire [31:0] rs2_val = xreg[rs2];

	integer i;

	always @(posedge clk or negedge rst_n) begin
		if (!rst_n) begin
			state       <= S_FETCH;
			pc          <= 32'h0;
			halted      <= 1'b0;
			exit_value  <= 32'h0;
			imem_addr   <= 32'h0;
			dmem_addr   <= 32'h0;
			dmem_wdata  <= 32'h0;
			dmem_wstrb  <= 4'h0;
			dmem_we     <= 1'b0;
			d_rd        <= 5'h0;
			for (i = 0; i < 32; i = i + 1)
				xreg[i] <= 32'h0;
		end else if (!run) begin
			state     <= S_FETCH;
			pc        <= 32'h0;
			imem_addr <= 32'h0;
			halted    <= 1'b0;
		end else if (!halted) begin

			dmem_we <= 1'b0;

			case (state)

			S_FETCH: begin
				imem_addr <= pc;
				state     <= S_DECODE;
			end

			S_DECODE: begin
				case (opcode)
				7'h13: begin
					// ADDI
					if (rd != 5'h0)
						xreg[rd] <= rs1_val + {{20{imm_i[11]}}, imm_i};
					pc        <= pc + 4;
					state     <= S_FETCH;
				end
				7'h03: begin
					// LBU rd, imm(rs1)
					dmem_addr <= rs1_val + {{20{imm_i[11]}}, imm_i};
					d_rd      <= rd;
					state     <= S_LOAD_WAIT;
				end
				7'h63: begin
					// BEQ rs1, rs2, imm
					if (f3 == 3'h0 && rs1_val == rs2_val)
						pc <= pc + {{19{imm_b[12]}}, imm_b};
					else
						pc <= pc + 4;
					state <= S_FETCH;
				end
				7'h67: begin
					// JALR rd, rs1, imm
					if ((rs1_val + {{20{imm_i[11]}}, imm_i}) == 32'h0) begin
						halted     <= 1'b1;
						exit_value <= xreg[10];
					end else begin
						pc <= rs1_val + {{20{imm_i[11]}}, imm_i};
					end
					if (rd != 5'h0)
						xreg[rd] <= pc + 4;
					state <= S_FETCH;
				end
				default: begin
					halted     <= 1'b1;
					exit_value <= 32'hDEADBEEF;
				end
				endcase
			end

			S_LOAD_WAIT: begin
				if (d_rd != 5'h0)
					xreg[d_rd] <= {24'h0, dmem_rdata[7:0]};
				pc    <= pc + 4;
				state <= S_FETCH;
			end

			default: state <= S_FETCH;
			endcase

			xreg[0] <= 32'h0;
		end
	end

endmodule
