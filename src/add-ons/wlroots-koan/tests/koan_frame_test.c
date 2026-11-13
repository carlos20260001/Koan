/*
 * SPDX-License-Identifier: MIT
 * Copyright © 2026 carlos20260001
 *
 * koan_frame: recycled memory for composed frames (server/koan_frame.h).
 *  - a released buffer is handed out again for the same size, not for
 *    another size;
 *  - buffers are 64-byte aligned;
 *  - at most four buffers and 512 MiB are kept, the oldest go first;
 *  - trim frees everything kept.
 */

#include <stdint.h>
#include <stdio.h>

#include "koan_frame.h"

static int sFailures;

#define CHECK(cond, ...) do { \
	if (!(cond)) { \
		fprintf(stderr, "FAIL %s:%d: ", __FILE__, __LINE__); \
		fprintf(stderr, __VA_ARGS__); \
		fputc('\n', stderr); \
		sFailures++; \
	} \
} while (0)

int
main(void)
{
	struct koan_frame_stats stats;

	void *a = koan_frame_alloc(4096);
	CHECK(a != NULL, "alloc failed");
	CHECK(((uintptr_t)a & 63) == 0, "frame %p not 64-byte aligned", a);
	koan_frame_release(a);
	void *b = koan_frame_alloc(4096);
	CHECK(b == a, "same size not reused (%p, then %p)", a, b);
	void *c = koan_frame_alloc(8192);
	CHECK(c != NULL && c != b, "another size got the kept buffer");
	koan_frame_get_stats(&stats);
	CHECK(stats.reused == 1 && stats.allocated == 2,
		"reused %llu allocated %llu (expected 1 and 2)",
		(unsigned long long)stats.reused,
		(unsigned long long)stats.allocated);
	koan_frame_release(b);
	koan_frame_release(c);

	/* Six released: only the four newest are kept. */
	void *frames[6];
	for (int i = 0; i < 6; i++)
		frames[i] = koan_frame_alloc(1000 + (size_t)i);
	for (int i = 0; i < 6; i++)
		koan_frame_release(frames[i]);
	koan_frame_get_stats(&stats);
	CHECK(stats.cached == 4, "%d kept (expected 4)", stats.cached);
	void *newest = koan_frame_alloc(1005);
	CHECK(newest == frames[5], "newest released not kept");
	koan_frame_release(newest);

	/* Two 300 MiB frames do not both fit in 512 MiB. */
	koan_frame_trim();
	const size_t big = (size_t)300 * 1024 * 1024;
	void *big1 = koan_frame_alloc(big);
	void *big2 = koan_frame_alloc(big);
	CHECK(big1 != NULL && big2 != NULL, "300 MiB alloc failed");
	koan_frame_release(big1);
	koan_frame_release(big2);
	koan_frame_get_stats(&stats);
	CHECK(stats.cached == 1 && stats.cached_bytes == big,
		"%d kept, %zu bytes (expected 1, %zu)", stats.cached,
		stats.cached_bytes, big);

	koan_frame_trim();
	koan_frame_get_stats(&stats);
	CHECK(stats.cached == 0 && stats.cached_bytes == 0,
		"after trim %d kept, %zu bytes", stats.cached, stats.cached_bytes);

	if (sFailures == 0)
		printf("koan_frame_test: all checks passed\n");
	return sFailures == 0 ? 0 : 1;
}
