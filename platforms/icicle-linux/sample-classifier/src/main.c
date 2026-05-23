// SPDX-License-Identifier: Apache-2.0
/*
 * sample-classifier/src/main.c — Icicle Kit (rv64gc) Linux user-space
 * driver for the MERLIN-V kernel module.
 *
 * Opens /dev/merlin, loads classifier_blob via MERLIN_PROG_LOAD,
 * runs MERLIN_PROG_TEST_RUN twice (once with an IPv4 ETH frame, once
 * with an RARP frame), and prints the verdict each time.
 *
 * Mirrors the ESP32-C3 Zephyr sample byte-for-byte except the
 * underlying transport is /dev/merlin ioctl rather than the in-Zephyr
 * function call.
 */

#define _POSIX_C_SOURCE 200809L
#define _GNU_SOURCE

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <stdint.h>

/* MERLIN ioctl interface (mirrors kernel/merlin/syscall.c). */
#define MERLIN_IOC_MAGIC   'M'
#define MERLIN_IOC_CMD     _IOWR(MERLIN_IOC_MAGIC, 1, struct merlin_ioc_args)

struct merlin_ioc_args {
	uint32_t cmd;
	uint32_t attr_sz;
	uint64_t attr_ptr;
};

/* Mirror of union merlin_attr from uapi/linux/merlin.h
 * (we declare just the two variants we use).
 */
#define MERLIN_OBJ_NAME_LEN 16

#define MERLIN_PROG_LOAD     0
#define MERLIN_PROG_TEST_RUN 1
#define MERLIN_PROFILE_LINUX_RV64 1
#define MERLIN_PROG_TYPE_XDP_V    5

union merlin_attr {
	struct {
		uint64_t elf_ptr;
		uint32_t elf_len;
		uint32_t prog_type;
		uint32_t expected_attach_type;
		uint32_t profile;
		uint32_t log_level;
		uint64_t log_buf;
		uint32_t log_size;
		uint32_t prog_flags;
		char     prog_name[MERLIN_OBJ_NAME_LEN];
		uint64_t license_ptr;
		uint32_t prog_btf_fd;
		uint32_t _pad0;
		uint64_t func_info;
		uint32_t func_info_rec_size;
		uint32_t func_info_cnt;
		uint64_t line_info;
		uint32_t line_info_rec_size;
		uint32_t line_info_cnt;
		uint32_t fd_array_uref_cnt;
		uint32_t _pad1;
		uint64_t fd_array;
	} prog_load;
	struct {
		uint32_t prog_fd;
		uint32_t retval;
		uint32_t data_size_in;
		uint32_t data_size_out;
		uint64_t data_in;
		uint64_t data_out;
		uint32_t repeat;
		uint32_t duration_ns;
		uint32_t ctx_size_in;
		uint32_t ctx_size_out;
		uint64_t ctx_in;
		uint64_t ctx_out;
		uint32_t flags;
		uint32_t cpu;
		uint32_t batch_size;
		uint32_t _pad;
	} test_run;
	uint8_t _pad_full[256];
} __attribute__((aligned(8)));

extern const uint8_t  classifier_blob[];
extern const uint32_t classifier_blob_len;

static const uint8_t pkt_ipv4[16] = {
	0xff,0xff,0xff,0xff,0xff,0xff,
	0x02,0x00,0x00,0x00,0x00,0x01,
	0x08,0x00,
	0x00,0x00,
};

static const uint8_t pkt_rarp[16] = {
	0xff,0xff,0xff,0xff,0xff,0xff,
	0x02,0x00,0x00,0x00,0x00,0x01,
	0x80,0x35,
	0x00,0x00,
};

static const char *verdict_str(uint32_t v)
{
	switch (v) {
	case 1: return "DROP";
	case 2: return "PASS";
	default: return "?";
	}
}

static int merlin_ioctl(int fd, uint32_t cmd, union merlin_attr *attr)
{
	struct merlin_ioc_args ioc = {
		.cmd      = cmd,
		.attr_sz  = sizeof(*attr),
		.attr_ptr = (uintptr_t)attr,
	};
	return ioctl(fd, MERLIN_IOC_CMD, &ioc);
}

int main(void)
{
	int fd, prog_fd, rc;
	union merlin_attr attr;
	char log[512] = {0};

	fd = open("/dev/merlin", O_RDWR);
	if (fd < 0) {
		fprintf(stderr, "open /dev/merlin: %s\n", strerror(errno));
		fprintf(stderr, "(hint: insmod kernel/merlin/merlin.ko first;"
				" run as root, or chmod 0666 /dev/merlin)\n");
		return 1;
	}
	printf("classifier: opened /dev/merlin\n");

	/* PROG_LOAD */
	memset(&attr, 0, sizeof(attr));
	attr.prog_load.elf_ptr   = (uintptr_t)classifier_blob;
	attr.prog_load.elf_len   = classifier_blob_len;
	attr.prog_load.prog_type = MERLIN_PROG_TYPE_XDP_V;
	attr.prog_load.profile   = MERLIN_PROFILE_LINUX_RV64;
	attr.prog_load.log_buf   = (uintptr_t)log;
	attr.prog_load.log_size  = sizeof(log);
	attr.prog_load.log_level = 1;
	strncpy(attr.prog_load.prog_name, "classifier",
		sizeof(attr.prog_load.prog_name) - 1);

	prog_fd = merlin_ioctl(fd, MERLIN_PROG_LOAD, &attr);
	if (prog_fd < 0) {
		fprintf(stderr, "MERLIN_PROG_LOAD: %s\n", strerror(errno));
		fprintf(stderr, "verifier log: %s\n", log);
		close(fd);
		return 1;
	}
	printf("classifier: loaded prog_fd=%d log='%s'\n", prog_fd, log);

	/* PROG_TEST_RUN — IPv4 packet */
	memset(&attr, 0, sizeof(attr));
	attr.test_run.prog_fd     = prog_fd;
	attr.test_run.ctx_size_in = sizeof(pkt_ipv4);
	attr.test_run.ctx_in      = (uintptr_t)pkt_ipv4;
	rc = merlin_ioctl(fd, MERLIN_PROG_TEST_RUN, &attr);
	if (rc < 0)
		fprintf(stderr, "TEST_RUN(ipv4): %s\n", strerror(errno));
	else
		printf("classifier: ETH/IPv4 packet -> retval=%u (%s) "
		       "duration=%u ns\n",
		       attr.test_run.retval, verdict_str(attr.test_run.retval),
		       attr.test_run.duration_ns);

	/* PROG_TEST_RUN — RARP packet */
	memset(&attr, 0, sizeof(attr));
	attr.test_run.prog_fd     = prog_fd;
	attr.test_run.ctx_size_in = sizeof(pkt_rarp);
	attr.test_run.ctx_in      = (uintptr_t)pkt_rarp;
	rc = merlin_ioctl(fd, MERLIN_PROG_TEST_RUN, &attr);
	if (rc < 0)
		fprintf(stderr, "TEST_RUN(rarp): %s\n", strerror(errno));
	else
		printf("classifier: ETH/RARP  packet -> retval=%u (%s) "
		       "duration=%u ns\n",
		       attr.test_run.retval, verdict_str(attr.test_run.retval),
		       attr.test_run.duration_ns);

	close(prog_fd);
	close(fd);
	printf("classifier: done\n");
	return 0;
}
