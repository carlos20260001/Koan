/*
 * Koan compatibility header: xf86drm.h
 *
 * Koan does not ship Linux kernel/libdrm headers. This minimal header
 * satisfies compilation of wlroots source files that include <xf86drm.h>
 * but do not actually call libdrm functions.
 *
 * This is a compatibility layer for the Koan port, not a replacement for
 * libdrm on systems where it is available.
 */

#ifndef XF86DRM_H
#define XF86DRM_H

#include <stdint.h>

/* DRM node types. */
#define DRM_NODE_RENDER   0
#define DRM_NODE_PRIMARY  1

/* DRM magic type. */
typedef uint32_t drm_magic_t;

/* Opaque DRM device handle. */
struct drm_device {
	int fd;
	uint32_t available_nodes;
	char *nodes[4];
};
typedef struct drm_device drmDevice;

/* No-op DRM API stubs for Koan (no DRM kernel driver). */
static inline int drmOpen(const char *name, int *fd) { (void)name; (void)fd; return -1; }
static inline void drmClose(int fd) { (void)fd; }
static inline void drmFreeDevice(void *devices) { (void)devices; }
static inline int drmGetNodeTypeFromFd(int fd) { (void)fd; return DRM_NODE_RENDER; }
static inline const char *drmGetRenderDeviceNameFromFd(int fd) { (void)fd; return NULL; }
static inline const char *drmGetFormatName(uint32_t format) { (void)format; return "unknown"; }
static inline int drmGetDevices2(uint32_t flags, drmDevice **devices, int max_devices) {
	(void)flags; (void)devices; (void)max_devices; return 0;
}
static inline int drmIsMaster(int fd) { (void)fd; return 0; }
static inline const char *drmGetDeviceNameFromFd2(int fd) { (void)fd; return NULL; }
static inline int drmGetMagic(int fd, drm_magic_t *magic) { (void)fd; (void)magic; return -1; }
static inline int drmAuthMagic(int fd, drm_magic_t magic) { (void)fd; (void)magic; return -1; }
static inline int drmGetDevice2(int fd, uint32_t flags, drmDevice **device) {
	(void)fd; (void)flags; (void)device; return -1;
}
static inline int drmPrimeFDToHandle(int fd, int prime_fd, uint32_t *handle) {
	(void)fd; (void)prime_fd; (void)handle; return -1;
}
static inline int drmCloseBufferHandle(int fd, uint32_t handle) {
	(void)fd; (void)handle; return -1;
}
static inline int drmGetDeviceFromDevId(uint64_t dev_id, uint32_t flags, drmDevice **device) {
	(void)dev_id; (void)flags; (void)device; return -1;
}

/* DRM capabilities and ioctls. */
#define DRM_CAP_SYNCOBJ_TIMELINE  0x35
#define DRM_IOCTL_SYNCOBJ_EVENTFD 0x40
#define DRM_SYNCOBJ_WAIT_FLAGS_WAIT_AVAILABLE  0x2

struct drm_syncobj_eventfd {
	uint32_t handle;
	int32_t fd;
	uint32_t flags;
	uint32_t pad;
};

static inline int drmGetCap(int fd, uint64_t capability, uint64_t *value) {
	(void)fd; (void)capability; (void)value; return -1;
}
static inline long drmIoctl(int fd, unsigned long request, void *arg) {
	(void)fd; (void)request; (void)arg; return -1;
}

#endif /* XF86DRM_H */
