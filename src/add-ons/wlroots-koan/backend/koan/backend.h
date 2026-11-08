/*
 * Koan platform backend for wlroots.
 *
 * Koan provides its own native display and input stack through app_server.
 * This backend exposes Koan displays as wlroots outputs so that Wayland
 * surfaces can be mapped to native Koan windows.
 */

#ifndef WLR_KOAN_BACKEND_H
#define WLR_KOAN_BACKEND_H

#include <wlr/backend.h>

struct wlr_koan_backend {
	struct wlr_backend backend;
};

#ifdef __cplusplus
extern "C" {
#endif

struct wlr_koan_backend *wlr_koan_backend_create(struct wl_display *display);
bool wlr_koan_output_create(struct wlr_backend *backend, int32_t width,
		int32_t height);

#ifdef __cplusplus
}
#endif

#endif /* WLR_KOAN_BACKEND_H */
