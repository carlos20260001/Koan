/*
 * Koan compatibility header: xf86drmMode.h
 *
 * Koan does not ship Linux kernel/libdrm headers. This minimal header
 * satisfies compilation of wlroots source files that include <xf86drmMode.h>
 * but do not actually call libdrm functions.
 *
 * This is a compatibility layer for the Koan port, not a replacement for
 * libdrm on systems where it is available.
 */

#ifndef XF86DRMMODE_H
#define XF86DRMMODE_H

#include <stdint.h>

/* No-op DRM mode API stubs for Koan (no DRM kernel driver). */
static inline int drmModeCreateLease(int fd, uint32_t *objects, int count,
		uint32_t flags, uint32_t *lessee_id) {
	(void)fd; (void)objects; (void)count; (void)flags; (void)lessee_id;
	return -1;
}

#endif /* XF86DRMMODE_H */
