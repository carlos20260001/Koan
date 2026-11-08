/*
 * wlroots-compat.h — compatibility shims for wayland APIs missing in
 * older versions available in the build container.
 *
 * wlroots 0.20.2 targets wayland >= 1.24.0. Koan's builder ships
 * wayland-server 1.23.x, so this header provides the small set of core
 * additions that wlroots uses:
 *
 *  - wl_resource_post_error_vargs() (implemented in wlroots-compat.c);
 *  - the version 1 wl_fixes interface (generated from compat/fixes.xml).
 *
 * Both are guarded by the wayland version so they disappear on newer
 * builders and cannot shadow the system definitions.
 *
 * NOTE: *_is_valid() validation functions for protocol enums are generated
 * by wayland-scanner. With wayland-scanner < 1.24 they live inside the same
 * include guard as the enum itself, which is why the public
 * <wayland-protocols/NAME-enum.h> headers are generated as forwarders to the
 * server protocol headers (see CMakeLists.txt).
 */

#ifndef WLR_KOAN_COMPAT_H
#define WLR_KOAN_COMPAT_H

#include <stdarg.h>
#include <stdint.h>
#include <string.h>
#include <strings.h>

#include <wayland-version.h>
#include <xkbcommon/xkbcommon.h>

struct wl_resource;

#if WAYLAND_VERSION_MAJOR < 1 \
	|| (WAYLAND_VERSION_MAJOR == 1 && WAYLAND_VERSION_MINOR < 24)
/* wayland-server < 1.24 has no wl_fixes core interface. */
#include "fixes-protocol.h"

/*
 * wl_resource_post_error_vargs() — missing from wayland-server 1.23.x.
 * Declared here; implemented in wlroots-compat.c.
 */
void wl_resource_post_error_vargs(struct wl_resource *resource,
	uint32_t code, const char *msg, va_list argp);
#endif

/* ffs() — may not be declared by <string.h>/<strings.h> under certain
 * feature-test-macro combinations (e.g. -std=gnu11 -D_POSIX_C_SOURCE=200809L
 * on some libc versions). Declared explicitly here as a compatibility shim
 * since this header is force-included to all wlroots sources. */
int ffs(int);

/* LED names added in libxkbcommon 1.8.0. The builder may ship 1.7.x, where
 * only CAPS/NUM/SCROLL exist. Values match upstream xkbcommon-names.h. */
#ifndef XKB_LED_NAME_COMPOSE
#define XKB_LED_NAME_COMPOSE "Compose"
#endif
#ifndef XKB_LED_NAME_KANA
#define XKB_LED_NAME_KANA "Kana"
#endif

#endif /* WLR_KOAN_COMPAT_H */
