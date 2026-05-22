// SPDX-License-Identifier: Apache-2.0
//
// host/merlin-fabric-load.c — userland tool that pushes a verified
// MERLIN-V image into a PolarFire-fabric soft RV32 core and reads
// back the result.
//
// Talks to the fabric via a UIO device (/dev/uioN) backed by the
// AXI-Lite slave at the address given in the devicetree.  The UIO
// region is mmap'd into the process and accessed as a normal
// pointer.
//
// Memory map (from rtl/axi_ctrl.v):
//
//   0x000  CTRL       wo  bit0 = run, bit1 = soft_reset
//   0x004  STATUS     ro  bit0 = halted, bit1 = running
//   0x008  EXIT       ro  exit_value
//   0x010  IMEM_WSEL  rw  next IMEM write index (words)
//   0x014  IMEM_WDATA wo  push one 32-bit insn
//   0x018  DMEM_WSEL  rw  next DMEM write index
//   0x01c  DMEM_WDATA wo  push one 32-bit data word
//
// Usage:
//
//   merlin-fabric-load [-u /dev/uio0] <classifier.bin>
//
// where classifier.bin is the raw .text section bytes (extract with
// objcopy or use the classifier_blob.c byte array as input).

#define _POSIX_C_SOURCE 200809L
#define _DEFAULT_SOURCE

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/stat.h>

#define CTRL_OFF       0x000
#define STATUS_OFF     0x004
#define EXIT_OFF       0x008
#define IMEM_WSEL_OFF  0x010
#define IMEM_WDATA_OFF 0x014
#define DMEM_WSEL_OFF  0x018
#define DMEM_WDATA_OFF 0x01c

#define UIO_MAP_SIZE   0x1000

static volatile uint32_t *axi;

static inline void wr(unsigned off, uint32_t v)
{
	axi[off >> 2] = v;
}

static inline uint32_t rd(unsigned off)
{
	return axi[off >> 2];
}

static int load_text(const char *path, uint8_t **out, size_t *out_len)
{
	int fd = open(path, O_RDONLY);
	struct stat st;
	if (fd < 0) { perror(path); return -1; }
	if (fstat(fd, &st) < 0) { perror("fstat"); close(fd); return -1; }
	*out = malloc(st.st_size);
	if (!*out) { close(fd); return -1; }
	if (read(fd, *out, st.st_size) != st.st_size) {
		perror("read"); free(*out); close(fd); return -1;
	}
	*out_len = st.st_size;
	close(fd);
	return 0;
}

static void upload_text(const uint8_t *text, size_t n)
{
	wr(IMEM_WSEL_OFF, 0);
	for (size_t i = 0; i + 4 <= n; i += 4) {
		uint32_t w = (uint32_t)text[i]
			   | ((uint32_t)text[i+1] << 8)
			   | ((uint32_t)text[i+2] << 16)
			   | ((uint32_t)text[i+3] << 24);
		wr(IMEM_WDATA_OFF, w);
	}
}

static void upload_ctx(const uint8_t *ctx, size_t n)
{
	wr(DMEM_WSEL_OFF, 0);
	uint8_t tail[4] = {0};
	size_t i;
	for (i = 0; i + 4 <= n; i += 4) {
		uint32_t w = (uint32_t)ctx[i]
			   | ((uint32_t)ctx[i+1] << 8)
			   | ((uint32_t)ctx[i+2] << 16)
			   | ((uint32_t)ctx[i+3] << 24);
		wr(DMEM_WDATA_OFF, w);
	}
	if (i < n) {
		memcpy(tail, ctx + i, n - i);
		wr(DMEM_WDATA_OFF,
		   (uint32_t)tail[0]
		 | ((uint32_t)tail[1] << 8)
		 | ((uint32_t)tail[2] << 16)
		 | ((uint32_t)tail[3] << 24));
	}
}

static uint32_t run_and_wait(unsigned timeout_us)
{
	wr(CTRL_OFF, 0x2);          // soft reset
	usleep(10);
	wr(CTRL_OFF, 0x0);          // release reset
	wr(CTRL_OFF, 0x1);          // start
	for (unsigned us = 0; us < timeout_us; us++) {
		if (rd(STATUS_OFF) & 0x1)
			return rd(EXIT_OFF);
		usleep(1);
	}
	fprintf(stderr, "merlin-fabric-load: timeout waiting for halt\n");
	return (uint32_t)-1;
}

int main(int argc, char **argv)
{
	const char *uio_path = "/dev/uio0";
	int opt;

	while ((opt = getopt(argc, argv, "u:")) != -1) {
		if (opt == 'u') uio_path = optarg;
		else {
			fprintf(stderr,
				"usage: %s [-u /dev/uio0] <text.bin>\n",
				argv[0]);
			return 2;
		}
	}
	if (optind >= argc) {
		fprintf(stderr,
			"usage: %s [-u /dev/uio0] <text.bin>\n", argv[0]);
		return 2;
	}

	uint8_t *text;
	size_t text_len;
	if (load_text(argv[optind], &text, &text_len) < 0)
		return 1;

	int fd = open(uio_path, O_RDWR);
	if (fd < 0) {
		fprintf(stderr, "open %s: %s\n", uio_path, strerror(errno));
		fprintf(stderr,
			"(hint: kernel must be built with CONFIG_UIO_PDRV_GENIRQ,\n"
			"  and the devicetree must declare the AXI-Lite slave\n"
			"  with compatible = \"generic-uio\";)\n");
		return 1;
	}
	axi = mmap(NULL, UIO_MAP_SIZE, PROT_READ | PROT_WRITE,
		   MAP_SHARED, fd, 0);
	if (axi == MAP_FAILED) {
		perror("mmap"); close(fd); return 1;
	}

	printf("fabric-load: opened %s, text=%zu bytes\n", uio_path, text_len);

	// Synthetic Ethernet/IPv4 frame for the worked example.
	static const uint8_t pkt_ipv4[16] = {
		0xff,0xff,0xff,0xff,0xff,0xff,
		0x02,0x00,0x00,0x00,0x00,0x01,
		0x08,0x00, 0x00,0x00,
	};
	static const uint8_t pkt_rarp[16] = {
		0xff,0xff,0xff,0xff,0xff,0xff,
		0x02,0x00,0x00,0x00,0x00,0x01,
		0x80,0x35, 0x00,0x00,
	};

	upload_text(text, text_len);

	upload_ctx(pkt_ipv4, sizeof(pkt_ipv4));
	uint32_t r1 = run_and_wait(1000);
	printf("fabric-load: ETH/IPv4 -> %u  (%s)\n", r1,
	       r1 == 2 ? "PASS" : r1 == 1 ? "DROP" : "??");

	upload_ctx(pkt_rarp, sizeof(pkt_rarp));
	uint32_t r2 = run_and_wait(1000);
	printf("fabric-load: ETH/RARP -> %u  (%s)\n", r2,
	       r2 == 2 ? "PASS" : r2 == 1 ? "DROP" : "??");

	munmap((void *)axi, UIO_MAP_SIZE);
	close(fd);
	free(text);
	return (r1 == 2 && r2 == 1) ? 0 : 1;
}
