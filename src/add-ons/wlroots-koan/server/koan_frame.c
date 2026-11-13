/*
 * SPDX-License-Identifier: MIT
 * Copyright © 2026 carlos20260001
 *
 * Recycled memory for composed window frames (koan_frame.h).
 */

#include <pthread.h>
#include <stdlib.h>
#include <string.h>

#include "koan_frame.h"

/* Each buffer starts with this header, one cache line, so the pixels after
 * it keep the 64 byte alignment. */
#define KOAN_FRAME_HEADER 64
#define KOAN_FRAME_MAGIC 0x4b4f414eu /* "KOAN" */

struct frame_header {
	size_t size;
	uint32_t magic;
};

/* Kept for reuse: enough for a window being composed, one queued and one
 * being imported, plus one; at most this many bytes (four 5K frames). */
#define KOAN_FRAME_CACHE_MAX 4
#define KOAN_FRAME_CACHE_BYTES ((size_t)512 * 1024 * 1024)

static pthread_mutex_t sLock = PTHREAD_MUTEX_INITIALIZER;
static struct frame_header *sCache[KOAN_FRAME_CACHE_MAX]; /* oldest first */
static int sCached;
static size_t sCachedBytes;
static uint64_t sAllocated;
static uint64_t sReused;

static void *frame_pixels(struct frame_header *header) {
	return (uint8_t *)header + KOAN_FRAME_HEADER;
}

static struct frame_header *frame_header_of(void *frame) {
	return (struct frame_header *)((uint8_t *)frame - KOAN_FRAME_HEADER);
}

/* Remove sCache[index], keeping the order. Lock held. */
static struct frame_header *cache_take(int index) {
	struct frame_header *header = sCache[index];
	memmove(&sCache[index], &sCache[index + 1],
		(size_t)(sCached - index - 1) * sizeof(sCache[0]));
	sCached--;
	sCachedBytes -= header->size;
	return header;
}

void *koan_frame_alloc(size_t size) {
	if (size == 0)
		size = 1;

	pthread_mutex_lock(&sLock);
	/* The newest of that size: its pages are the most likely still hot. */
	for (int i = sCached - 1; i >= 0; i--) {
		if (sCache[i]->size == size) {
			struct frame_header *header = cache_take(i);
			sReused++;
			pthread_mutex_unlock(&sLock);
			return frame_pixels(header);
		}
	}
	pthread_mutex_unlock(&sLock);

	void *memory = NULL;
	if (size > SIZE_MAX - KOAN_FRAME_HEADER
			|| posix_memalign(&memory, KOAN_FRAME_HEADER,
				KOAN_FRAME_HEADER + size) != 0)
		return NULL;
	struct frame_header *header = memory;
	header->size = size;
	header->magic = KOAN_FRAME_MAGIC;

	pthread_mutex_lock(&sLock);
	sAllocated++;
	pthread_mutex_unlock(&sLock);
	return frame_pixels(header);
}

void koan_frame_release(void *frame) {
	if (frame == NULL)
		return;
	struct frame_header *header = frame_header_of(frame);
	if (header->magic != KOAN_FRAME_MAGIC)
		abort(); /* not from koan_frame_alloc(), or released twice */

	struct frame_header *evicted[KOAN_FRAME_CACHE_MAX + 1];
	int evictedCount = 0;

	pthread_mutex_lock(&sLock);
	if (header->size > KOAN_FRAME_CACHE_BYTES) {
		evicted[evictedCount++] = header;
	} else {
		/* Make room: drop the oldest until it fits. */
		while (sCached > 0 && (sCached == KOAN_FRAME_CACHE_MAX
				|| sCachedBytes + header->size > KOAN_FRAME_CACHE_BYTES))
			evicted[evictedCount++] = cache_take(0);
		sCache[sCached++] = header;
		sCachedBytes += header->size;
	}
	pthread_mutex_unlock(&sLock);

	/* Free outside the lock: unmapping a large buffer is slow. */
	for (int i = 0; i < evictedCount; i++) {
		evicted[i]->magic = 0;
		free(evicted[i]);
	}
}

void koan_frame_trim(void) {
	struct frame_header *evicted[KOAN_FRAME_CACHE_MAX];
	int evictedCount = 0;

	pthread_mutex_lock(&sLock);
	while (sCached > 0)
		evicted[evictedCount++] = cache_take(0);
	pthread_mutex_unlock(&sLock);

	for (int i = 0; i < evictedCount; i++) {
		evicted[i]->magic = 0;
		free(evicted[i]);
	}
}

void koan_frame_get_stats(struct koan_frame_stats *stats) {
	pthread_mutex_lock(&sLock);
	stats->allocated = sAllocated;
	stats->reused = sReused;
	stats->cached_bytes = sCachedBytes;
	stats->cached = sCached;
	pthread_mutex_unlock(&sLock);
}
