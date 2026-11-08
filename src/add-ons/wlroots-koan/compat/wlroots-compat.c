/*
 * wlroots-compat.c — compatibility implementations for wayland APIs
 * missing in older versions available in the build container.
 *
 * Provides wl_resource_post_error_vargs() (missing from wayland-server
 * 1.23.x). The declaration is version-gated in wlroots-compat.h.
 */

#include <stdarg.h>
#include <stdio.h>

#include <wayland-server-core.h>
#include <wayland-version.h>

#include "wlroots-compat.h"

#if WAYLAND_VERSION_MAJOR < 1 \
	|| (WAYLAND_VERSION_MAJOR == 1 && WAYLAND_VERSION_MINOR < 24)
void wl_resource_post_error_vargs(struct wl_resource *resource,
	uint32_t code, const char *msg, va_list argp)
{
	char buf[256];

	vsnprintf(buf, sizeof(buf), msg, argp);
	wl_resource_post_error(resource, code, "%s", buf);
}
#endif
