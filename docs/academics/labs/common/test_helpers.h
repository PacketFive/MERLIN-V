/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * common/test_helpers.h — student-friendly assert macros.
 *
 * PROVIDED — do not modify.
 */
#ifndef MERLIN_LABS_TEST_HELPERS_H
#define MERLIN_LABS_TEST_HELPERS_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TH_FAIL(fmt, ...) do {                                        \
	fprintf(stderr, "%s:%d: FAIL: " fmt "\n",                     \
		__FILE__, __LINE__, ##__VA_ARGS__);                   \
	exit(1);                                                      \
} while (0)

#define TH_ASSERT(cond) do {                                          \
	if (!(cond))                                                  \
		TH_FAIL("assertion failed: %s", #cond);               \
} while (0)

#define TH_ASSERT_EQ_U32(got, want) do {                              \
	uint32_t _g = (uint32_t)(got);                                \
	uint32_t _w = (uint32_t)(want);                               \
	if (_g != _w)                                                 \
		TH_FAIL("expected 0x%08x, got 0x%08x", _w, _g);       \
} while (0)

#define TH_ASSERT_EQ_STR(got, want) do {                              \
	const char *_g = (got);                                       \
	const char *_w = (want);                                      \
	if (strcmp(_g, _w) != 0)                                      \
		TH_FAIL("expected \"%s\", got \"%s\"", _w, _g);       \
} while (0)

#define TH_PASS(name) do {                                            \
	fprintf(stderr, "  [PASS] %s\n", (name));                     \
} while (0)

#endif /* MERLIN_LABS_TEST_HELPERS_H */
