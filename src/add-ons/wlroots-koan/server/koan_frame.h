/*
 * SPDX-License-Identifier: MIT
 * Copyright © 2026 carlos20260001
 *
 * Memory for composed window frames, recycled between frames.
 *
 * Koan composes every frame of a window into a buffer it hands to
 * app_server (set_buffer), which releases it once the pixels are imported.
 * A frame of a full-screen window at 5K is about 100 MB: allocating it
 * with calloc() and freeing it each frame mapped and unmapped that much
 * memory 25 times a second, and the page faults and the TLB shootdowns of
 * the unmaps took more time than composing (QEMU, 6384x4076 at 200%).
 * Released buffers are kept and handed out again for a frame of the same
 * size.
 *
 * Thread-safe: frames are allocated on the Wayland thread and released on
 * app_server's. The memory is not cleared.
 */

#ifndef KOAN_FRAME_H
#define KOAN_FRAME_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* A buffer of at least size bytes, aligned to 64 bytes: a released one of
 * exactly that size if there is one, else a new one. NULL on failure. */
void *koan_frame_alloc(size_t size);

/* Give back a buffer from koan_frame_alloc(); NULL is ignored. Every
 * owner of a set_buffer frame releases it this way, never with free(). */
void koan_frame_release(void *frame);

/* Free every kept buffer. */
void koan_frame_trim(void);

struct koan_frame_stats {
	uint64_t allocated;    /* buffers newly allocated */
	uint64_t reused;       /* buffers handed out again */
	size_t cached_bytes;   /* bytes kept for reuse now */
	int cached;            /* buffers kept for reuse now */
};

void koan_frame_get_stats(struct koan_frame_stats *stats);

#ifdef __cplusplus
}
#endif

#endif /* KOAN_FRAME_H */
