/*
 * SPDX-License-Identifier: MIT
 * Copyright © 2026 carlos20260001
 */

/*
 * Koan Wayland server lifecycle.
 *
 * Owns the Wayland display endpoint (Unix domain socket), initialises wlroots
 * protocol infrastructure, accepts multiple concurrent client connections,
 * and forwards surface lifecycle events to the Koan window adapter.
 *
 * app_server owns the native Window, decorations, focus, stacking and display
 * presentation. wlroots-koan owns the Wayland protocol resources and
 * client-side surface state.
 */

#ifndef KOAN_SERVER_H
#define KOAN_SERVER_H

#include <wayland-server-core.h>

struct wl_display;
struct wl_event_loop;

#ifdef __cplusplus
extern "C" {
#endif

/* Create the Wayland display and register all core protocol globals
 * (compositor, xdg-shell, shm, seat, subcompositor, data device manager,
 * output). Returns the display on success, NULL on failure. */
struct wl_display *koan_server_create(void);

/* Add a Unix domain socket to the display and return its name (e.g. "wayland-1").
 * The socket name should be published via WAYLAND_DISPLAY so clients can find it. */
const char *koan_server_add_socket(struct wl_display *display);

/* Start the Koan backend (enumerates native outputs/inputs). */
bool koan_server_start(void);

/* Get the wl_event_loop associated with the display, for integration with the
 * app_server main event loop. */
struct wl_event_loop *koan_server_get_event_loop(struct wl_display *display);

/* Get the event-loop file descriptor. Returns -1 on failure. */
int koan_server_get_fd(struct wl_display *display);

/* Dispatch Wayland events with the given timeout (ms), then flush pending
 * client writes. Returns false on error. */
bool koan_server_dispatch(struct wl_display *display, int timeout_ms);

/* Run the Wayland event loop until terminated (wl_display_terminate).
 * Intended for standalone testing or a dedicated dispatch thread. */
void koan_server_run(struct wl_display *display);

/* Set the dimensions and scale of the Koan wl_output. Call this from
 * app_server once real display configuration is available. */
bool koan_server_set_output_dimensions(int32_t width, int32_t height,
				       float scale);

/* Input from app_server's native window, on the Wayland thread. The
 * toplevel must be known to be alive (the adapter checks). Coordinates are
 * relative to the window content (the xdg window geometry). */
struct wlr_xdg_toplevel;
void koan_server_pointer_motion(struct wlr_xdg_toplevel *toplevel,
				uint32_t time, double x, double y);
void koan_server_pointer_leave(struct wlr_xdg_toplevel *toplevel);
void koan_server_pointer_button(struct wlr_xdg_toplevel *toplevel,
				uint32_t time, uint32_t button, bool pressed);
void koan_server_pointer_axis(struct wlr_xdg_toplevel *toplevel,
			      uint32_t time, bool horizontal, double steps);
/* An xdg_popup was mapped or unmapped: give a grabbing popup the keyboard,
 * or hand it back to what opened it. Wayland thread only. */
struct wlr_xdg_popup;
void koan_server_popup_keyboard(struct wlr_xdg_popup *popup, bool mapped);

void koan_server_keyboard_focus(struct wlr_xdg_toplevel *toplevel,
				bool focused);
void koan_server_keyboard_key(struct wlr_xdg_toplevel *toplevel,
			      uint32_t time, uint32_t key, bool pressed);

/* Destroy the display, all clients, and all wlroots state. */
void koan_server_destroy(struct wl_display *display);

#ifdef __cplusplus
}
#endif

#endif /* KOAN_SERVER_H */
