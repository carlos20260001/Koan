/*
 * SPDX-License-Identifier: MIT
 * Copyright © 2026 carlos20260001
 */

/*
 * Koan window adapter for wlroots.
 *
 * Connects wlroots Wayland surfaces (xdg_toplevel) to native Koan windows
 * managed by app_server. Each Wayland surface becomes a native Koan window.
 *
 * app_server owns the native Window. wlroots-koan owns the Wayland protocol
 * resources and client-side surface state. An explicit relationship is
 * maintained between the wlr_xdg_toplevel and the native window handle so
 * that destroyed Wayland resources never leave dangling native window
 * references, and repeated surface commits never create duplicate windows.
 *
 * Threading model (see koan_queue.h):
 *  - All adapter callbacks are invoked from the Wayland dispatch thread.
 *  - Callbacks do NOT manipulate native window objects directly. Instead they
 *    push commands onto the thread-safe command queue. app_server's thread
 *    drains the queue and performs the actual native operations.
 *  - Buffer ownership: set_buffer receives Koan-owned memory (a copy of the
 *    client pixels). The wlroots buffer pointer is NEVER handed across the
 *    queue. The receiver frees the copied memory after copying it into the
 *    native window bitmap.
 */

#ifndef KOAN_ADAPTER_H
#define KOAN_ADAPTER_H

#ifdef __cplusplus
extern "C" {
#endif

/* wlroots public headers are C-only and have no extern "C" guards. Include
 * them inside the block so C++ consumers (app_server) get C linkage. */
#include <wlr/util/box.h>
#include <wlr/types/wlr_xdg_shell.h>

struct wlr_xdg_toplevel;
struct koan_queue;

/*
 * Callbacks provided by app_server to create and manage native windows.
 * All callbacks are invoked on the Wayland dispatch thread. They must not
 * block and must not manipulate native window objects directly; they push
 * commands onto the command queue instead.
 *
 * Contract:
 *  - Thread: invoked on the Wayland dispatch thread.
 *  - Data ownership: caller (adapter) retains ownership of toplevel pointers;
 *    the receiver must not retain them beyond the callback return.
 *  - Pointers remain valid only for the duration of the callback.
 *  - Errors: return non-zero to indicate failure; the adapter logs and
 *    continues (no surface is left in a corrupted state).
 */
struct koan_window_callbacks {
	void *userdata;

	/* Create a native window for a mapped xdg_toplevel.
	 * Pushes a KOAN_CMD_CREATE_WINDOW command. The native window handle is
	 * returned asynchronously via the command's window_out field once
	 * app_server has processed it. */
	void (*create_window)(void *userdata, struct koan_queue *queue,
			      struct wlr_xdg_toplevel *toplevel,
			      int32_t width, int32_t height);

	/* Destroy the native window associated with an unmapped/destroyed
	 * xdg_toplevel. Pushes a KOAN_CMD_DESTROY_WINDOW command. */
	void (*destroy_window)(void *userdata, struct koan_queue *queue,
			       void *window);

	/* Set the window title. Pushes a KOAN_CMD_SET_TITLE command. */
	void (*set_title)(void *userdata, struct koan_queue *queue,
			  void *window, const char *title);

	/* Set the window size (configure). Pushes a KOAN_CMD_SET_SIZE command. */
	void (*set_size)(void *userdata, struct koan_queue *queue,
			 void *window, int32_t width, int32_t height);

	/* Deliver a client buffer to the native window content.
	 * 'data' is Koan-owned memory (a copy of the client pixels). The
	 * receiver must copy it into the native bitmap and free 'data'.
	 * The pointer is valid only for the duration of this callback. */
	void (*set_buffer)(void *userdata, struct koan_queue *queue,
			   void *window, void *data,
			   int32_t width, int32_t height, int32_t stride,
			   uint32_t format);

	/* Set window visibility (map/unmap). Pushes a KOAN_CMD_SET_VISIBILITY
	 * command. */
	void (*set_visibility)(void *userdata, struct koan_queue *queue,
			       void *window, bool visible);

	/* The client asked to (un)maximize or (un)set fullscreen
	 * (xdg_toplevel.set_/unset_maximized, set_/unset_fullscreen).
	 * maximized/fullscreen: 1 enter, 0 leave, -1 not part of this request.
	 * The window manager decides and must answer with a
	 * KOAN_CMD_TOPLEVEL_CONFIGURE (xdg-shell requires a configure in reply
	 * even when the state is refused). May be NULL: Koan then replies
	 * with the current state. Only called once the native window exists. */
	void (*request_state)(void *userdata, struct koan_queue *queue,
			      void *window, int32_t maximized, int32_t fullscreen);

	/* The client asked to be minimized (xdg_toplevel.set_minimized).
	 * xdg-shell has no matching unminimize request and no reply: the
	 * window manager hides the native window the way its minimize button
	 * does, and the user restores it from the window list. May be NULL:
	 * the request is then ignored, which xdg-shell allows. Only called
	 * once the native window exists. */
	void (*request_minimize)(void *userdata, struct koan_queue *queue,
				 void *window);

	/* Show a mapped xdg_popup as a native window above its toplevel's
	 * (no frame, never takes focus, like a native menu). (x, y): its
	 * top-left in the toplevel's window-content coordinates. Reply with
	 * KOAN_CMD_POPUP_READY. The popup's pixels then arrive through
	 * set_buffer and it goes away through destroy_window, both with the
	 * handle POPUP_READY returned. May be NULL: popups are then not shown
	 * (they still get configures and frame callbacks). */
	void (*create_popup)(void *userdata, struct koan_queue *queue,
			     struct wlr_xdg_popup *popup,
			     struct wlr_xdg_toplevel *toplevel,
			     int32_t x, int32_t y, int32_t width, int32_t height);

	/* A shown popup moved; same coordinates as create_popup. */
	void (*move_popup)(void *userdata, struct koan_queue *queue,
			   void *window, int32_t x, int32_t y);

	/* Wake app_server after the Wayland thread queued a command for it
	 * outside the callbacks above (koan_test_v1). May be NULL. */
	void (*wake_app_server)(void *userdata);
};

/*
 * Registration context for app_server. Binds the callback interface to a
 * command queue so the adapter can marshal requests across threads.
 */
struct koan_adapter {
	struct koan_window_callbacks cbs;
	struct koan_queue *queue;
	void *userdata;
};

/* Register app_server's window-management callbacks and command queue. */
void koan_adapter_set_window_callbacks(
		const struct koan_window_callbacks *cb,
		struct koan_queue *queue, void *userdata);

/* Initialise the adapter: listen for new xdg_toplevels on the given shell. */
void koan_adapter_init(struct wlr_xdg_shell *xdg_shell,
		       struct koan_queue *queue, void *userdata);

/* Called by the server when a new xdg_toplevel is created. */
void koan_adapter_handle_new_toplevel(struct wlr_xdg_toplevel *toplevel);

/* Called by the server when a toplevel is mapped (ready to display). */
void koan_adapter_toplevel_mapped(struct wlr_xdg_toplevel *toplevel);

/* Called by the server when a toplevel is unmapped. */
void koan_adapter_toplevel_unmapped(struct wlr_xdg_toplevel *toplevel);

/* Called by the server when a toplevel surface commits new content. */
void koan_adapter_toplevel_committed(struct wlr_xdg_toplevel *toplevel);

/* The native window's content box in root-surface coordinates: the xdg
 * window geometry clamped to the live extents of the surface tree. This is
 * the box set_buffer delivers, and app_server's pointer positions are
 * relative to its origin. Wayland thread only. */
struct wlr_box koan_adapter_window_box(struct wlr_xdg_toplevel *toplevel);

/* xdg_popup lifecycle, from koan_server.c (Wayland thread). new: track it
 * (its unmap and destroy are followed here); committed: a mapped popup
 * committed new content. */
void koan_adapter_popup_new(struct wlr_xdg_popup *popup);
void koan_adapter_popup_committed(struct wlr_xdg_popup *popup);
/* Server shutdown: drop the popup records without calling back. */
void koan_adapter_forget_popups(void);

/* Calls koan_window_callbacks.wake_app_server, if set. */
void koan_adapter_wake_app_server(void);

/* Called by the server when a toplevel is destroyed. */
void koan_adapter_toplevel_destroyed(struct wlr_xdg_toplevel *toplevel);

/* Drain pending commands from the queue. Called by the Wayland thread when
 * the queue's wake fd is readable. Returns the number of commands processed. */
int koan_adapter_drain_commands(void);

/* Get the queue's wake-up file descriptor for event loop registration. */
int koan_adapter_get_wake_fd(void);

/* Get the command queue owned by the adapter. */
struct koan_queue *koan_adapter_queue(void);

#ifdef __cplusplus
}
#endif

#endif /* KOAN_ADAPTER_H */
