// SPDX-License-Identifier: GPL-2.0-or-later
//
// rtl/merlin_core_picorv32.v - real RV32I soft core for synthesis.
//
// Drop-in replacement for rtl/merlin_core.v that wraps PicoRV32
// (vendored at rtl/vendor/picorv32/picorv32.v, ISC license).
//
// Same external port list as merlin_core.v, so merlin_top.v doesn't
// change.  Internally:
//
//   * PicoRV32 (RV32I, no IRQ, no MUL/DIV, no compressed) provides a
//     unified valid/ready memory bus with mem_instr to flag fetches.
//   * A small bus adapter routes that bus to the existing split
//     imem (read-only) and dmem (RW) ports based on the top address
//     nibble:
//        0x0xxx_xxxx -> IMEM   (instruction fetches and read-only data
//                               reads; mem_instr is checked but is
//                               not the routing key — addr is)
//        0x1xxx_xxxx -> DMEM
//        0x2xxx_xxxx -> MMIO_HALT (write: latch exit_value, set halted)
//   * Because the IMEM / DMEM BRAMs have a 1-cycle read latency we
//     hold mem_ready low for one cycle after mem_valid, then assert
//     it together with mem_rdata.  Writes complete in one cycle.
//
// Halt convention.  Real RISC-V has no "halt" instruction.  Programs
// running here are expected to be wrapped in a shim that writes the
// result (the value the classifier would have returned in a0) to
// 0x20000000 and then loops forever; the wrapper detects that write,
// latches the value into `exit_value`, and asserts `halted`.  The
// existing testbench (tb/tb_merlin_top_picorv32.v) builds such a shim.

`default_nettype none

module merlin_core
(input wire clk,
input wire rst_n,
input wire run,

output reg [31 : 0] imem_addr,
input wire [31 : 0] imem_rdata,

output reg [31 : 0] dmem_addr,
output reg [31 : 0] dmem_wdata,
output reg [3 : 0] dmem_wstrb,
output reg dmem_we,
input wire [31 : 0] dmem_rdata,

output reg halted,
output reg [31 : 0] exit_value);

// PicoRV32 sees a single bus.  resetn = rst_n & run, so flipping
// run low forces a CPU reset; halted is sticky and also forces
// reset (the core stops fetching once we latch the exit value).
wire cpu_resetn = rst_n & run & ~halted;

wire cpu_trap;
wire cpu_mem_valid;
wire cpu_mem_instr;
wire [31 : 0] cpu_mem_addr;
wire [31 : 0] cpu_mem_wdata;
wire [3 : 0] cpu_mem_wstrb;

reg [31 : 0] cpu_mem_rdata;
reg cpu_mem_ready;

// PicoRV32 has many auxiliary outputs we don't use.  Tie inputs
// off and leave outputs floating.
wire cpu_mem_la_read, cpu_mem_la_write;
wire [31 : 0] cpu_mem_la_addr;
wire [31 : 0] cpu_mem_la_wdata;
wire [3 : 0] cpu_mem_la_wstrb;
wire cpu_pcpi_valid;
wire [31 : 0] cpu_pcpi_insn;
wire [31 : 0] cpu_pcpi_rs1, cpu_pcpi_rs2;
wire [31 : 0] cpu_eoi;
wire cpu_trace_valid;
wire [35 : 0] cpu_trace_data;

picorv32 #(.ENABLE_COUNTERS(0),
.ENABLE_COUNTERS64(0),
.ENABLE_REGS_16_31(1),
.ENABLE_REGS_DUALPORT(1),
.LATCHED_MEM_RDATA(0),
.TWO_STAGE_SHIFT(1),
.BARREL_SHIFTER(0),
.TWO_CYCLE_COMPARE(0),
.TWO_CYCLE_ALU(0),
.COMPRESSED_ISA(0),
.CATCH_MISALIGN(1),
.CATCH_ILLINSN(1),
.ENABLE_PCPI(0),
.ENABLE_MUL(0),
.ENABLE_FAST_MUL(0),
.ENABLE_DIV(0),
.ENABLE_IRQ(0),
.ENABLE_IRQ_QREGS(0),
.ENABLE_IRQ_TIMER(0),
.ENABLE_TRACE(0),
.REGS_INIT_ZERO(1),
.PROGADDR_RESET(32'h0000_0000),
.STACKADDR(32'h1000_0400))
u_cpu(.clk(clk),
.resetn(cpu_resetn),
.trap(cpu_trap),

.mem_valid(cpu_mem_valid),
.mem_instr(cpu_mem_instr),
.mem_ready(cpu_mem_ready),
.mem_addr(cpu_mem_addr),
.mem_wdata(cpu_mem_wdata),
.mem_wstrb(cpu_mem_wstrb),
.mem_rdata(cpu_mem_rdata),

.mem_la_read(cpu_mem_la_read),
.mem_la_write(cpu_mem_la_write),
.mem_la_addr(cpu_mem_la_addr),
.mem_la_wdata(cpu_mem_la_wdata),
.mem_la_wstrb(cpu_mem_la_wstrb),

.pcpi_valid(cpu_pcpi_valid),
.pcpi_insn(cpu_pcpi_insn),
.pcpi_rs1(cpu_pcpi_rs1),
.pcpi_rs2(cpu_pcpi_rs2),
.pcpi_wr(1'b0),
.pcpi_rd(32'h0),
.pcpi_wait(1'b0),
.pcpi_ready(1'b0),

.irq(32'h0),
.eoi(cpu_eoi),

.trace_valid(cpu_trace_valid),
.trace_data(cpu_trace_data));

// --- Bus adapter ---
//
// State machine: when mem_valid is asserted we are either doing a
// write (wstrb != 0) or a read.  A write completes in one cycle:
// drive the BRAM enables this cycle, assert mem_ready next cycle.
// A read needs the BRAM's 1-cycle latency: drive the address this
// cycle, sample rdata + assert mem_ready next cycle.
localparam [2 : 0] S_IDLE = 3'd0;
localparam [2 : 0] S_READ_DRIVE = 3'd1;
localparam [2 : 0] S_READ_LATCH = 3'd2;
localparam [2 : 0] S_DONE = 3'd3;
reg [2 : 0] bus_state;
reg last_was_imem;
reg last_was_dmem;

// Address decode (combinational)
wire addr_imem = (cpu_mem_addr[31 : 28] == 4'h0);
wire addr_dmem = (cpu_mem_addr[31 : 28] == 4'h1);
wire addr_halt = (cpu_mem_addr[31 : 28] == 4'h2);
wire is_write = |cpu_mem_wstrb;

always @(posedge clk or negedge rst_n) begin
	if (!rst_n) begin
		bus_state <= S_IDLE;
		cpu_mem_ready <= 1'b0;
		cpu_mem_rdata <= 32'h0;
		imem_addr <= 32'h0;
		dmem_addr <= 32'h0;
		dmem_wdata <= 32'h0;
		dmem_wstrb <= 4'h0;
		dmem_we <= 1'b0;
		halted <= 1'b0;
		exit_value <= 32'h0;
		last_was_imem <= 1'b0;
		last_was_dmem <= 1'b0;
	end else if (!run) begin
		// Soft reset path: keep memory side quiet but do not
		// clear halted or exit_value (the host reads them).
		bus_state <= S_IDLE;
		cpu_mem_ready <= 1'b0;
		dmem_we <= 1'b0;
		halted <= 1'b0;
	end else begin
		// Defaults each cycle
		cpu_mem_ready <= 1'b0;
		dmem_we <= 1'b0;

		case (bus_state)
			S_IDLE: begin
				if (cpu_mem_valid && !cpu_mem_ready) begin
					if (is_write) begin
						if (addr_dmem) begin
							dmem_addr <=
							cpu_mem_addr;
							dmem_wdata <=
							cpu_mem_wdata;
							dmem_wstrb <=
							cpu_mem_wstrb;
							dmem_we <= 1'b1;
						end else if (addr_halt) begin
							halted <= 1'b1;
							exit_value <=
							cpu_mem_wdata;
						end
						// Acknowledge writes immediately.
						cpu_mem_ready <= 1'b1;
						bus_state <= S_DONE;
					end else begin
						// Read: drive BRAM address this cycle.
						last_was_imem <= addr_imem;
						last_was_dmem <= addr_dmem;
						if (addr_imem)
						imem_addr <=
						cpu_mem_addr;
						if (addr_dmem)
						dmem_addr <=
						cpu_mem_addr;
						bus_state <= S_READ_DRIVE;
					end
				end
			end
			S_READ_DRIVE: begin
				// imem_addr/dmem_addr were just registered. The BRAM
				// is clocking them in at this posedge; r_data will be
				// valid at the NEXT posedge.
				bus_state <= S_READ_LATCH;
			end
			S_READ_LATCH: begin
				// r_data is now valid.  Forward + ack.
				if (last_was_imem)
				cpu_mem_rdata <= imem_rdata;
				else if (last_was_dmem)
				cpu_mem_rdata <= dmem_rdata;
				else
				cpu_mem_rdata <= 32'hDEADBEEF;
				cpu_mem_ready <= 1'b1;
				bus_state <= S_DONE;
			end
			S_DONE: begin
				// One-cycle handshake completes.  Back to IDLE.
				bus_state <= S_IDLE;
			end
			default:
			bus_state <= S_IDLE;
		endcase

		// PicoRV32 traps on illegal insn / misalign.  Treat as
		// halt with sentinel.
		if (cpu_trap && !halted) begin
			halted <= 1'b1;
			exit_value <= 32'hDEAD_BEEF;
		end
	end
end
endmodule
