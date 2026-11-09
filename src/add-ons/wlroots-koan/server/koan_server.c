/*
 * Koan Wayland server lifecycle.
 *
 * Owns the Wayland display endpoint (Unix domain socket), initialises wlroots
 * protocol infrastructure (compositor, xdg-shell, shm, seat, subcompositor,
 * data device manager, output), accepts multiple concurrent client connections,
 * and forwards surface lifecycle events to the Koan window adapter.
 *
 * app_server owns the native Window, decorations, focus, stacking and display
 * presentation. wlroots-koan owns the Wayland protocol resources and
 * client-side surface state. A thread-safe command queue (koan_queue) carries
 * requests between the Wayland dispatch thread and the app_server thread.
 */

#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <wayland-server-core.h>
#include <wlr/render/allocator.h>
#include <wlr/render/wlr_renderer.h>
#include <wlr/interfaces/wlr_output.h>
#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_data_device.h>
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/types/wlr_keyboard.h>
#include <wlr/interfaces/wlr_keyboard.h>
#include <xkbcommon/xkbcommon.h>
#include <wlr/types/wlr_subcompositor.h>
#include <wlr/types/wlr_xdg_shell.h>
#include <wlr/types/wlr_xdg_decoration_v1.h>
#include <wlr/types/wlr_server_decoration.h>
#include <wlr/types/wlr_shm.h>
#include <wlr/util/log.h>

#include "koan_server.h"
#include "backend/koan/backend.h"
#include "koan_adapter.h"
#include "koan_queue.h"

struct koan_server {
	struct wl_display *display;
	struct wlr_backend *backend;
	struct wlr_koan_backend *koan_backend;
	struct wlr_renderer *renderer;
	struct wlr_allocator *allocator;
	struct wlr_compositor *compositor;
	struct wlr_subcompositor *subcompositor;
	struct wlr_data_device_manager *data_device_manager;
	struct wlr_seat *seat;
	/* Keyboard input arrives from app_server (the native window's
	 * B_KEY_DOWN/UP translated to evdev codes), not from a backend device:
	 * a virtual keyboard keeps the xkb state and feeds the seat. */
	struct wlr_keyboard keyboard;
	bool keyboard_ready;
	struct wl_listener keyboard_key;
	struct wl_listener keyboard_modifiers;
	struct wlr_cursor *cursor;
	struct wlr_xdg_shell *xdg_shell;
	/* app_server draws the window frame (title tab, controls, stacking):
	 * ask clients not to draw their own. xdg-decoration for GTK4/Qt/most
	 * clients, the KDE protocol for GTK3 (Firefox). */
	struct wlr_xdg_decoration_manager_v1 *xdg_decoration_manager;
	struct wlr_server_decoration_manager *kde_decoration_manager;
	struct wl_list decorations;   /* struct koan_decoration.link */
	struct wl_listener new_toplevel_decoration;
	struct wlr_scene *scene;
	struct wlr_scene_output_layout *scene_layout;
	struct wlr_output_layout *output_layout;
	struct wlr_output *output;    /* synthetic Koan output, owned here */

	struct wl_list toplevels;     /* struct koan_toplevel.link */

	struct wl_listener new_input;
	struct wl_listener new_surface;
	struct wl_listener new_xdg_toplevel;
	struct wl_listener new_xdg_popup;

	/* Surface lifecycle listeners are owned by koan_adapter. */

	const char *socket_name;
	bool started;

	/* Command queue for app_server communication. Owned by this server. */
	struct koan_queue *queue;

	/* Event source for the queue wake-up fd, registered on the Wayland
	 * event loop so app_server commands are drained on the Wayland thread. */
	struct wl_event_source *queue_fd_source;
};

static struct koan_server g_server = {0};

static void koan_server_handle_new_xdg_toplevel(struct wl_listener *listener,
		void *data);

static void koan_server_handle_new_xdg_popup(struct wl_listener *listener,
		void *data);

/* -------------------------------------------------------------------------- */
/* Surface lifecycle events                                                   */
/* -------------------------------------------------------------------------- */

struct koan_toplevel {
	struct wl_list link;
	struct koan_server *server;
	struct wlr_xdg_toplevel *toplevel;
	struct wlr_surface *surface;
	struct wl_listener surface_map;
	struct wl_listener surface_unmap;
	struct wl_listener surface_destroy;
};

static void koan_toplevel_destroy(struct koan_toplevel *kt) {
	if (!kt)
		return;
	wl_list_remove(&kt->surface_map.link);
	wl_list_remove(&kt->surface_unmap.link);
	wl_list_remove(&kt->surface_destroy.link);
	wl_list_remove(&kt->link);
	free(kt);
}

static void koan_surface_handle_map(struct wl_listener *listener, void *data) {
	struct koan_toplevel *kt =
		wl_container_of(listener, kt, surface_map);
	struct koan_server *server = kt->server;

	wl_list_insert(&server->toplevels, &kt->link);

	koan_adapter_handle_new_toplevel(kt->toplevel);
	koan_adapter_toplevel_mapped(kt->toplevel);
	wlr_log(WLR_INFO, "Koan adapter: xdg_toplevel mapped (%s)",
		kt->toplevel->title ? kt->toplevel->title : "(null)");
}

static void koan_surface_handle_unmap(struct wl_listener *listener,
		void *data) {
	struct koan_toplevel *kt =
		wl_container_of(listener, kt, surface_unmap);
	/* Re-init after removal: destroy removes the link again, and
	 * wl_list_remove() leaves NULL pointers behind. */
	wl_list_remove(&kt->link);
	wl_list_init(&kt->link);
	koan_adapter_toplevel_unmapped(kt->toplevel);
	wlr_log(WLR_INFO, "Koan adapter: xdg_toplevel unmapped (%s)",
		kt->toplevel->title ? kt->toplevel->title : "(null)");
}

/* -------------------------------------------------------------------------- */
/* Server-side decorations                                                    */
/* -------------------------------------------------------------------------- */

struct koan_decoration {
	struct wl_list link;
	struct wlr_xdg_toplevel_decoration_v1 *decoration;
	struct wl_listener destroy;
	struct wl_listener request_mode;
};

/* wlr_xdg_toplevel_decoration_v1_set_mode() schedules a configure, which
 * asserts the xdg surface is initialized; clients usually create the
 * decoration before their first commit. Set the mode now if possible,
 * otherwise koan_decorations_initial_commit() does it. */
static void koan_decoration_apply(struct koan_decoration *kd) {
	if (kd->decoration->toplevel->base->initialized)
		wlr_xdg_toplevel_decoration_v1_set_mode(kd->decoration,
			WLR_XDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE);
}

static void koan_decoration_handle_request_mode(struct wl_listener *listener,
		void *data) {
	(void)data;
	struct koan_decoration *kd = wl_container_of(listener, kd, request_mode);
	/* Server side whatever the client asks: app_server owns the frame. */
	koan_decoration_apply(kd);
}

static void koan_decoration_handle_destroy(struct wl_listener *listener,
		void *data) {
	(void)data;
	struct koan_decoration *kd = wl_container_of(listener, kd, destroy);
	wl_list_remove(&kd->destroy.link);
	wl_list_remove(&kd->request_mode.link);
	wl_list_remove(&kd->link);
	free(kd);
}

static void koan_server_handle_new_toplevel_decoration(
		struct wl_listener *listener, void *data) {
	(void)listener;
	struct wlr_xdg_toplevel_decoration_v1 *decoration = data;
	struct koan_decoration *kd = calloc(1, sizeof(*kd));
	if (!kd)
		return;
	kd->decoration = decoration;
	kd->destroy.notify = koan_decoration_handle_destroy;
	wl_signal_add(&decoration->events.destroy, &kd->destroy);
	kd->request_mode.notify = koan_decoration_handle_request_mode;
	wl_signal_add(&decoration->events.request_mode, &kd->request_mode);
	wl_list_insert(&g_server.decorations, &kd->link);
	koan_decoration_apply(kd);
}

/* The toplevel's initial commit initialized its xdg surface: apply the
 * modes that could not be set when the decoration was created. */
static void koan_decorations_initial_commit(struct wlr_xdg_toplevel *toplevel) {
	struct koan_decoration *kd;
	wl_list_for_each(kd, &g_server.decorations, link) {
		if (kd->decoration->toplevel == toplevel)
			koan_decoration_apply(kd);
	}
}

/* Commits are tracked on every surface, not only on toplevel roots:
 * clients draw into subsurfaces (Firefox renders the whole window into one)
 * and a desynchronized subsurface commits on its own. */
struct koan_surface {
	struct wlr_surface *surface;
	struct wl_listener commit;
	struct wl_listener destroy;
};

static void koan_send_frame_done(struct wlr_surface *surface, int sx, int sy,
		void *data) {
	(void)sx;
	(void)sy;
	wlr_surface_send_frame_done(surface, data);
}

static void koan_any_surface_handle_commit(struct wl_listener *listener,
		void *data) {
	(void)data;
	struct koan_surface *ks = wl_container_of(listener, ks, commit);
	struct wlr_surface *surface = ks->surface;

	/* A synchronized subsurface's state is applied with its parent's
	 * commit, which delivers the whole tree. */
	struct wlr_subsurface *subsurface =
		wlr_subsurface_try_from_wlr_surface(surface);
	if (subsurface && subsurface->synchronized)
		return;

	struct wlr_surface *root = wlr_surface_get_root_surface(surface);

	/* xdg_popup: like a toplevel, the initial commit must be answered
	 * with a configure (geometry from the client's positioner), or menus
	 * and tooltips never open. Popups have no native window yet, so
	 * their pixels do not reach app_server; still complete their frame
	 * callbacks so clients drawing into them do not stall. */
	struct wlr_xdg_popup *popup = wlr_xdg_popup_try_from_wlr_surface(root);
	if (popup) {
		if (surface == root && popup->base->initial_commit) {
			wlr_xdg_surface_schedule_configure(popup->base);
			return;
		}
		if (root->mapped) {
			struct timespec now;
			clock_gettime(CLOCK_MONOTONIC, &now);
			wlr_surface_for_each_surface(root, koan_send_frame_done, &now);
		}
		return;
	}

	struct wlr_xdg_toplevel *toplevel =
		wlr_xdg_toplevel_try_from_wlr_surface(root);
	if (!toplevel)
		return;

	/* wlroots does not configure new toplevels by itself: the compositor
	 * must answer the initial (buffer-less) commit with a configure, or
	 * the client waits forever and never attaches a buffer or maps.
	 * 0x0 lets the client choose its own size. */
	if (surface == root && toplevel->base->initial_commit) {
		koan_decorations_initial_commit(toplevel);
		wlr_xdg_toplevel_set_size(toplevel, 0, 0);
		return;
	}
	if (!root->mapped)
		return;

	koan_adapter_toplevel_committed(toplevel);

	/* The pixels were copied out on commit, so every surface of the tree
	 * may draw its next frame now. Clients that throttle on
	 * wl_surface.frame (GTK, Firefox) otherwise stop after one frame. */
	struct timespec now;
	clock_gettime(CLOCK_MONOTONIC, &now);
	wlr_surface_for_each_surface(root, koan_send_frame_done, &now);
}

static void koan_any_surface_handle_destroy(struct wl_listener *listener,
		void *data) {
	(void)data;
	struct koan_surface *ks = wl_container_of(listener, ks, destroy);
	wl_list_remove(&ks->commit.link);
	wl_list_remove(&ks->destroy.link);
	free(ks);
}

static void koan_server_handle_new_surface(struct wl_listener *listener,
		void *data) {
	(void)listener;
	struct wlr_surface *surface = data;
	struct koan_surface *ks = calloc(1, sizeof(*ks));
	if (!ks) {
		wlr_log(WLR_ERROR, "Koan: failed to track a new surface");
		return;
	}
	ks->surface = surface;
	ks->commit.notify = koan_any_surface_handle_commit;
	wl_signal_add(&surface->events.commit, &ks->commit);
	ks->destroy.notify = koan_any_surface_handle_destroy;
	wl_signal_add(&surface->events.destroy, &ks->destroy);
}

static void koan_surface_handle_destroy(struct wl_listener *listener,
		void *data) {
	struct koan_toplevel *kt =
		wl_container_of(listener, kt, surface_destroy);
	koan_adapter_toplevel_destroyed(kt->toplevel);
	koan_toplevel_destroy(kt);
}

static void koan_server_handle_new_xdg_toplevel(struct wl_listener *listener,
		void *data) {
	struct wlr_xdg_toplevel *toplevel = data;
	struct koan_server *server = &g_server;
	struct wlr_surface *surface = toplevel->base->surface;

	if (!surface) {
		wlr_log(WLR_ERROR, "Koan: xdg_toplevel has no surface");
		return;
	}

	struct koan_toplevel *kt = calloc(1, sizeof(*kt));
	if (!kt) {
		return;
	}
	kt->server = server;
	kt->toplevel = toplevel;
	kt->surface = surface;
	/* Only mapped toplevels are on server->toplevels; keep the link valid
	 * so unmap/destroy can remove it unconditionally. */
	wl_list_init(&kt->link);

	/* Surface lifecycle events. */
	kt->surface_map.notify = koan_surface_handle_map;
	wl_signal_add(&surface->events.map, &kt->surface_map);
	kt->surface_unmap.notify = koan_surface_handle_unmap;
	wl_signal_add(&surface->events.unmap, &kt->surface_unmap);
	kt->surface_destroy.notify = koan_surface_handle_destroy;
	wl_signal_add(&surface->events.destroy, &kt->surface_destroy);
}

static void koan_server_handle_new_xdg_popup(struct wl_listener *listener,
		void *data) {
	(void)listener;
	(void)data;
}

/* -------------------------------------------------------------------------- */
/* Backend input                                                              */
/* -------------------------------------------------------------------------- */

static void koan_server_handle_new_input(struct wl_listener *listener,
		void *data) {
	struct koan_server *server = &g_server;
	struct wlr_input_device *device = data;
	switch (device->type) {
	case WLR_INPUT_DEVICE_POINTER:
		if (server->cursor != NULL)
			wlr_cursor_attach_input_device(server->cursor, device);
		break;
	default:
		break;
	}

}

/* -------------------------------------------------------------------------- */
/* Input from app_server                                                      */
/* -------------------------------------------------------------------------- */

static const struct wlr_keyboard_impl koan_keyboard_impl = {
	.name = "koan-keyboard",
};

static void koan_server_handle_keyboard_key(struct wl_listener *listener,
		void *data) {
	(void)listener;
	struct wlr_keyboard_key_event *event = data;
	wlr_seat_keyboard_notify_key(g_server.seat, event->time_msec,
		event->keycode, event->state);
}

static void koan_server_handle_keyboard_modifiers(struct wl_listener *listener,
		void *data) {
	(void)listener;
	(void)data;
	wlr_seat_keyboard_notify_modifiers(g_server.seat,
		&g_server.keyboard.modifiers);
}

/* Optional: without an xkb keymap the seat offers no keyboard. The layout
 * comes from the usual XKB_DEFAULT_* variables (default: us). */
static void koan_server_create_keyboard(struct koan_server *server) {
	struct xkb_context *context = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
	if (!context) {
		wlr_log(WLR_ERROR, "Koan: no xkb context; keyboard disabled");
		return;
	}
	struct xkb_keymap *keymap = xkb_keymap_new_from_names(context, NULL,
		XKB_KEYMAP_COMPILE_NO_FLAGS);
	xkb_context_unref(context);
	if (!keymap) {
		wlr_log(WLR_ERROR, "Koan: no xkb keymap; keyboard disabled");
		return;
	}

	wlr_keyboard_init(&server->keyboard, &koan_keyboard_impl,
		"koan-keyboard");
	bool ok = wlr_keyboard_set_keymap(&server->keyboard, keymap);
	xkb_keymap_unref(keymap);
	if (!ok) {
		wlr_keyboard_finish(&server->keyboard);
		wlr_log(WLR_ERROR, "Koan: setting the keymap failed; keyboard "
			"disabled");
		return;
	}
	/* Clients repeat keys themselves from this; app_server's own repeats
	 * are dropped by the bridge. */
	wlr_keyboard_set_repeat_info(&server->keyboard, 25, 600);

	server->keyboard_key.notify = koan_server_handle_keyboard_key;
	wl_signal_add(&server->keyboard.events.key, &server->keyboard_key);
	server->keyboard_modifiers.notify = koan_server_handle_keyboard_modifiers;
	wl_signal_add(&server->keyboard.events.modifiers,
		&server->keyboard_modifiers);
	wlr_seat_set_keyboard(server->seat, &server->keyboard);
	server->keyboard_ready = true;
}

struct koan_find_surface {
	struct wlr_surface *target;
	double x, y;
	bool found;
};

static void koan_find_surface_iter(struct wlr_surface *surface, int sx, int sy,
		void *data) {
	struct koan_find_surface *f = data;
	if (surface == f->target) {
		f->x = sx;
		f->y = sy;
		f->found = true;
	}
}

/* Whether the seat's pointer focus is in this toplevel's surface tree, and
 * where that surface sits relative to the root. */
static bool koan_pointer_focus_in(struct wlr_xdg_toplevel *toplevel,
		double *ox, double *oy) {
	struct wlr_surface *focus = g_server.seat->pointer_state.focused_surface;
	if (!focus || wlr_surface_get_root_surface(focus)
			!= toplevel->base->surface)
		return false;
	struct koan_find_surface f = { focus, 0, 0, false };
	wlr_surface_for_each_surface(toplevel->base->surface,
		koan_find_surface_iter, &f);
	*ox = f.x;
	*oy = f.y;
	return f.found;
}

void koan_server_pointer_motion(struct wlr_xdg_toplevel *toplevel,
		uint32_t time, double x, double y) {
	struct wlr_seat *seat = g_server.seat;
	if (!seat || !toplevel->base->initialized)
		return;

	/* Window-content coordinates -> root surface coordinates. */
	double rx = x + toplevel->base->geometry.x;
	double ry = y + toplevel->base->geometry.y;

	/* Implicit grab: while a button is held the surface that got the
	 * press keeps the pointer, even outside it (drags, scrollbars). */
	double ox, oy;
	if (seat->pointer_state.button_count > 0
			&& koan_pointer_focus_in(toplevel, &ox, &oy)) {
		wlr_seat_pointer_notify_motion(seat, time, rx - ox, ry - oy);
		wlr_seat_pointer_notify_frame(seat);
		return;
	}

	double sx, sy;
	struct wlr_surface *surface = wlr_xdg_surface_surface_at(toplevel->base,
		rx, ry, &sx, &sy);
	if (surface) {
		if (seat->pointer_state.focused_surface != surface)
			wlr_seat_pointer_notify_enter(seat, surface, sx, sy);
		wlr_seat_pointer_notify_motion(seat, time, sx, sy);
	} else if (koan_pointer_focus_in(toplevel, &ox, &oy)) {
		/* Over the window but outside every surface (input region). */
		wlr_seat_pointer_notify_clear_focus(seat);
	}
	wlr_seat_pointer_notify_frame(seat);
}

void koan_server_pointer_leave(struct wlr_xdg_toplevel *toplevel) {
	struct wlr_seat *seat = g_server.seat;
	double ox, oy;
	if (!seat || seat->pointer_state.button_count > 0
			|| !koan_pointer_focus_in(toplevel, &ox, &oy))
		return;
	wlr_seat_pointer_notify_clear_focus(seat);
	wlr_seat_pointer_notify_frame(seat);
}

void koan_server_pointer_button(struct wlr_xdg_toplevel *toplevel,
		uint32_t time, uint32_t button, bool pressed) {
	struct wlr_seat *seat = g_server.seat;
	double ox, oy;
	if (!seat || !koan_pointer_focus_in(toplevel, &ox, &oy))
		return;
	wlr_seat_pointer_notify_button(seat, time, button, pressed
		? WL_POINTER_BUTTON_STATE_PRESSED : WL_POINTER_BUTTON_STATE_RELEASED);
	wlr_seat_pointer_notify_frame(seat);
}

void koan_server_pointer_axis(struct wlr_xdg_toplevel *toplevel,
		uint32_t time, bool horizontal, double steps) {
	struct wlr_seat *seat = g_server.seat;
	double ox, oy;
	if (!seat || !koan_pointer_focus_in(toplevel, &ox, &oy))
		return;
	/* One wheel click: 15 units (libinput's default), 120 in value120. */
	wlr_seat_pointer_notify_axis(seat, time, horizontal
			? WL_POINTER_AXIS_HORIZONTAL_SCROLL
			: WL_POINTER_AXIS_VERTICAL_SCROLL,
		steps * 15.0, (int32_t)(steps * 120.0), WL_POINTER_AXIS_SOURCE_WHEEL,
		WL_POINTER_AXIS_RELATIVE_DIRECTION_IDENTICAL);
	wlr_seat_pointer_notify_frame(seat);
}

void koan_server_keyboard_focus(struct wlr_xdg_toplevel *toplevel,
		bool focused) {
	struct wlr_seat *seat = g_server.seat;
	if (!seat || !g_server.keyboard_ready || !toplevel->base->surface)
		return;
	struct wlr_surface *root = toplevel->base->surface;
	if (focused) {
		struct wlr_keyboard *kb = &g_server.keyboard;
		wlr_seat_keyboard_notify_enter(seat, root, kb->keycodes,
			kb->num_keycodes, &kb->modifiers);
	} else if (seat->keyboard_state.focused_surface == root) {
		/* Deactivation of a window that already lost focus to another
		 * one must not take the focus away from the new window. */
		wlr_seat_keyboard_notify_clear_focus(seat);
	}
}

void koan_server_keyboard_key(struct wlr_xdg_toplevel *toplevel,
		uint32_t time, uint32_t key, bool pressed) {
	struct wlr_seat *seat = g_server.seat;
	if (!seat || !g_server.keyboard_ready)
		return;
	/* Keys go to the active native window; make sure it has focus. */
	if (seat->keyboard_state.focused_surface != toplevel->base->surface)
		koan_server_keyboard_focus(toplevel, true);
	struct wlr_keyboard_key_event event = {
		.time_msec = time,
		.keycode = key,
		.update_state = true,
		.state = pressed ? WL_KEYBOARD_KEY_STATE_PRESSED
			: WL_KEYBOARD_KEY_STATE_RELEASED,
	};
	wlr_keyboard_notify_key(&g_server.keyboard, &event);
}

/* Drain app_server -> Wayland commands from the queue. Called when the
 * queue's wake-up fd is readable on the Wayland event loop. */
static int koan_server_handle_queue_fd(int fd, uint32_t mask, void *data) {
	(void)fd;
	(void)mask;
	(void)data;

	/* Drain the pipe. */
	uint8_t buf[16];
	while (read(fd, buf, sizeof(buf)) > 0)
		;

	/* Drain and process all pending app_server commands. */
	koan_adapter_drain_commands();

	return 1;
}

/* -------------------------------------------------------------------------- */
/* Output                                                                     */
/* -------------------------------------------------------------------------- */

static bool koan_output_commit(struct wlr_output *output,
		const struct wlr_output_state *state) {
	(void)output;
	(void)state;
	return true;
}

static bool koan_server_create_output(struct koan_server *server) {
	if (!server->output_layout || !server->scene_layout)
		return false;

	struct wlr_output *output = calloc(1, sizeof(*output));
	if (!output)
		return false;

	/* wlr_output_init() asserts impl->commit != NULL, and asserts are
	 * active in every Vitruvian build type (defs.cmake never defines
	 * NDEBUG). A NULL commit aborted app_server at startup. Koan does not
	 * render output frames itself (app_server presents), so commit just
	 * accepts the state. destroy stays NULL: wlr_output_destroy() then
	 * runs wlr_output_finish() and free()s the calloc'ed output. */
	static const struct wlr_output_impl output_impl = {
		.commit = koan_output_commit,
	};

	wlr_output_init(output, server->backend, &output_impl,
		wl_display_get_event_loop(server->display), NULL);

	/* Add a placeholder mode (1920x1080 @ 60Hz). app_server should update
	 * this with real display dimensions. */
	struct wlr_output_state state;
	wlr_output_state_init(&state);
	wlr_output_state_set_enabled(&state, true);
	/* Enabling without a mode fails output_basic_test() ("zero mode"). */
	wlr_output_state_set_custom_mode(&state, 1920, 1080, 60000);
	wlr_output_state_set_scale(&state, 1.0f);
	if (!wlr_output_commit_state(output, &state))
		wlr_log(WLR_ERROR, "Koan output: initial commit failed");
	wlr_output_state_finish(&state);

	/* wlr_cursor_attach_output_layout() creates a wlr_output_cursor for
	 * every output and wlr_output_cursor_set_buffer() asserts
	 * output->renderer != NULL, aborting app_server at startup. Give the
	 * output the server's renderer/allocator. This is done after the
	 * initial commit so enabling the output does not allocate a primary
	 * swapchain buffer Koan never presents. */
	if (!wlr_output_init_render(output, server->allocator,
			server->renderer)) {
		wlr_log(WLR_ERROR, "Koan output: failed to initialise rendering");
		wlr_output_destroy(output);
		return false;
	}

	wlr_output_create_global(output, server->display);
	wlr_output_layout_add(server->output_layout, output, 0, 0);
	server->output = output;
	wlr_log(WLR_INFO, "Koan output: created (placeholder dimensions)");
	return true;
}

bool koan_server_set_output_dimensions(int32_t width, int32_t height,
		float scale) {
	struct koan_server *server = &g_server;
	if (!server->backend || width <= 0 || height <= 0)
		return false;

	wlr_log(WLR_INFO, "Koan output: dimensions set to %dx%d scale=%.2f",
		width, height, scale);
	/* TODO: apply to the wlr_output once real output management is wired. */
	(void)server;
	(void)width;
	(void)height;
	(void)scale;
	return true;
}

/* -------------------------------------------------------------------------- */
/* Public API                                                                 */
/* -------------------------------------------------------------------------- */

struct wl_display *koan_server_create(void) {
	struct koan_server *server = &g_server;
	wl_list_init(&server->toplevels);
	wl_list_init(&server->new_input.link);
	wl_list_init(&server->new_surface.link);
	wl_list_init(&server->keyboard_key.link);
	wl_list_init(&server->keyboard_modifiers.link);
	server->keyboard_ready = false;
	wl_list_init(&server->new_toplevel_decoration.link);
	wl_list_init(&server->decorations);
	wl_list_init(&server->new_xdg_toplevel.link);
	wl_list_init(&server->new_xdg_popup.link);

	/* Create the command queue for app_server communication. */
	server->queue = koan_queue_create();
	if (!server->queue) {
		wlr_log(WLR_ERROR, "Failed to create command queue");
		return NULL;
	}

	server->display = wl_display_create();
	if (!server->display) {
		wlr_log(WLR_ERROR, "Failed to create Wayland display");
		koan_queue_destroy(server->queue);
		server->queue = NULL;
		return NULL;
	}
	wlr_log(WLR_INFO, "Koan Wayland server: display created");

	/* Create the Koan backend (native display/input). */
	server->koan_backend = wlr_koan_backend_create(server->display);
	if (!server->koan_backend) {
		wlr_log(WLR_ERROR, "Failed to create Koan backend");
		koan_queue_destroy(server->queue);
		server->queue = NULL;
		wl_display_destroy(server->display);
		server->display = NULL;
		return NULL;
	}
	server->backend = &server->koan_backend->backend;

	/* Autocreates a renderer (pixman on Koan, since only pixman is built). */
	server->renderer = wlr_renderer_autocreate(server->backend);
	if (!server->renderer) {
		wlr_log(WLR_ERROR, "Failed to create renderer");
		koan_queue_destroy(server->queue);
		server->queue = NULL;
		free(server->koan_backend);
		server->koan_backend = NULL;
		server->backend = NULL;
		wl_display_destroy(server->display);
		server->display = NULL;
		return NULL;
	}

	/* Create an allocator bridging the backend and renderer. */
	server->allocator = wlr_allocator_autocreate(server->backend,
		server->renderer);
	if (!server->allocator) {
		wlr_log(WLR_ERROR, "Failed to create allocator");
		koan_queue_destroy(server->queue);
		server->queue = NULL;
		wlr_renderer_destroy(server->renderer);
		server->renderer = NULL;
		free(server->koan_backend);
		server->koan_backend = NULL;
		server->backend = NULL;
		wl_display_destroy(server->display);
		server->display = NULL;
		return NULL;
	}

	/* Expose wl_shm (and linux-dmabuf, when the renderer has a DRM fd)
	 * to clients. This is the only wl_shm global: creating another one
	 * advertised wl_shm twice. */
	if (!wlr_renderer_init_wl_display(server->renderer, server->display)) {
		wlr_log(WLR_ERROR, "Failed to initialise renderer Wayland globals");
		koan_queue_destroy(server->queue);
		server->queue = NULL;
		wlr_allocator_destroy(server->allocator);
		wlr_renderer_destroy(server->renderer);
		free(server->koan_backend);
		server->koan_backend = NULL;
		server->backend = NULL;
		server->renderer = NULL;
		server->allocator = NULL;
		wl_display_destroy(server->display);
		server->display = NULL;
		return NULL;
	}

	/* Create the core Wayland globals. */
	server->compositor = wlr_compositor_create(server->display, 5,
		server->renderer);
	server->subcompositor = wlr_subcompositor_create(server->display);
	server->data_device_manager =
		wlr_data_device_manager_create(server->display);
	if (!server->compositor || !server->subcompositor ||
			!server->data_device_manager) {
		wlr_log(WLR_ERROR, "Failed to create core Wayland globals");
		koan_server_destroy(server->display);
		return NULL;
	}
	server->new_surface.notify = koan_server_handle_new_surface;
	wl_signal_add(&server->compositor->events.new_surface,
		&server->new_surface);

	/* Output layout for tracking screens. These are required by the
	 * scene/cursor and the synthetic output; fail cleanly if any cannot
	 * be created rather than passing NULL into wlroots APIs. */
	server->output_layout = wlr_output_layout_create(server->display);
	server->scene = wlr_scene_create();
	if (!server->output_layout || !server->scene) {
		wlr_log(WLR_ERROR, "Failed to create output layout/scene");
		koan_server_destroy(server->display);
		return NULL;
	}
	server->scene_layout =
		wlr_scene_attach_output_layout(server->scene, server->output_layout);
	if (!server->scene_layout) {
		wlr_log(WLR_ERROR, "Failed to attach scene output layout");
		koan_server_destroy(server->display);
		return NULL;
	}

	/* Create a wl_output for the Koan display. */
	koan_server_create_output(server);

	/* XDG shell for application windows. */
	server->xdg_shell = wlr_xdg_shell_create(server->display, 3);
	if (!server->xdg_shell) {
		wlr_log(WLR_ERROR, "Failed to create xdg-shell");
		koan_server_destroy(server->display);
		return NULL;
	}

	/* Server-side decorations. Optional: without them clients just draw
	 * their own frame inside the native window. */
	server->xdg_decoration_manager =
		wlr_xdg_decoration_manager_v1_create(server->display);
	if (server->xdg_decoration_manager) {
		server->new_toplevel_decoration.notify =
			koan_server_handle_new_toplevel_decoration;
		wl_signal_add(
			&server->xdg_decoration_manager->events.new_toplevel_decoration,
			&server->new_toplevel_decoration);
	} else {
		wlr_log(WLR_ERROR, "Koan: failed to create xdg-decoration manager");
	}
	server->kde_decoration_manager =
		wlr_server_decoration_manager_create(server->display);
	if (server->kde_decoration_manager)
		wlr_server_decoration_manager_set_default_mode(
			server->kde_decoration_manager,
			WLR_SERVER_DECORATION_MANAGER_MODE_SERVER);
	else
		wlr_log(WLR_ERROR, "Koan: failed to create KDE decoration manager");
	server->new_xdg_toplevel.notify = koan_server_handle_new_xdg_toplevel;
	wl_signal_add(&server->xdg_shell->events.new_toplevel,
		&server->new_xdg_toplevel);
	server->new_xdg_popup.notify = koan_server_handle_new_xdg_popup;
	wl_signal_add(&server->xdg_shell->events.new_popup,
		&server->new_xdg_popup);
	/* Cursor management. Optional: absence degrades pointer forwarding
	 * but must not crash server startup. */
	server->cursor = wlr_cursor_create();
	if (server->cursor && server->output_layout)
		wlr_cursor_attach_output_layout(server->cursor, server->output_layout);

	/* Seat for input. Optional: absence degrades input capability
	 * advertisement but must not crash server startup. */
	server->seat = wlr_seat_create(server->display, "seat0");
	if (server->seat) {
		server->new_input.notify = koan_server_handle_new_input;
		wl_signal_add(&server->backend->events.new_input, &server->new_input);
		koan_server_create_keyboard(server);
		/* Pointer and keyboard input come from app_server's native
		 * windows (koan_server_pointer_*, koan_server_keyboard_*). */
		uint32_t caps = WL_SEAT_CAPABILITY_POINTER;
		if (server->keyboard_ready)
			caps |= WL_SEAT_CAPABILITY_KEYBOARD;
		wlr_seat_set_capabilities(server->seat, caps);
	} else {
		wlr_log(WLR_ERROR, "Koan Wayland server: failed to create seat");
	}

	/* Initialise the Koan window adapter with the command queue. */
	koan_adapter_init(server->xdg_shell, server->queue, server);

	/* Register the queue wake-up fd on the Wayland event loop so app_server
	 * commands (e.g. KOAN_CMD_WINDOW_READY) are drained on the Wayland
	 * thread. */
	int queue_fd = koan_queue_get_wake_fd(server->queue);
	if (queue_fd >= 0) {
		struct wl_event_loop *loop = wl_display_get_event_loop(server->display);
		server->queue_fd_source = wl_event_loop_add_fd(loop, queue_fd,
			WL_EVENT_READABLE, koan_server_handle_queue_fd, server);
	}

	wlr_log(WLR_INFO, "Koan Wayland server: protocol globals registered");
	return server->display;
}

const char *koan_server_add_socket(struct wl_display *display) {
	if (!display)
		return NULL;
	/* The session (janus_session) exports WAYLAND_DISPLAY to every app it
	 * starts, so bind exactly that name: clients only find the server
	 * through it. wl_display_add_socket(display, NULL) uses
	 * $WAYLAND_DISPLAY. Fall back to an automatic name if it is unset or
	 * taken; clients then need WAYLAND_DISPLAY set by hand. */
	const char *socket = NULL;
	const char *wanted = getenv("WAYLAND_DISPLAY");
	if (wanted != NULL && wanted[0] != '\0') {
		if (wl_display_add_socket(display, NULL) == 0)
			socket = wanted;
		else
			wlr_log(WLR_ERROR, "Failed to bind WAYLAND_DISPLAY=%s; "
				"falling back to an automatic socket name", wanted);
	}
	if (!socket)
		socket = wl_display_add_socket_auto(display);
	if (!socket) {
		wlr_log(WLR_ERROR, "Failed to add Wayland socket");
		return NULL;
	}
	g_server.socket_name = socket;
	wlr_log(WLR_INFO, "Koan Wayland server: socket %s", socket);
	return socket;
}

bool koan_server_start(void) {
	if (!g_server.backend)
		return false;
	if (!wlr_backend_start(g_server.backend)) {
		wlr_log(WLR_ERROR, "Koan Wayland server: failed to start backend");
		return false;
	}
	g_server.started = true;
	wlr_log(WLR_INFO, "Koan Wayland server: backend started");
	return true;
}

struct wl_event_loop *koan_server_get_event_loop(struct wl_display *display) {
	if (!display)
		return NULL;
	return wl_display_get_event_loop(display);
}

int koan_server_get_fd(struct wl_display *display) {
	struct wl_event_loop *loop = koan_server_get_event_loop(display);
	if (!loop)
		return -1;
	return wl_event_loop_get_fd(loop);
}

bool koan_server_dispatch(struct wl_display *display, int timeout_ms) {
	struct wl_event_loop *loop = koan_server_get_event_loop(display);
	if (!loop)
		return false;
	int ret = wl_event_loop_dispatch(loop, timeout_ms);
	if (ret < 0)
		return false;
	wl_display_flush_clients(display);
	return true;
}

void koan_server_destroy(struct wl_display *display) {
	if (!display)
		return;

	struct koan_server *server = &g_server;

	/* Destroy all toplevels (removes surface listeners). */
	while (!wl_list_empty(&server->toplevels)) {
		struct koan_toplevel *kt =
			wl_container_of(server->toplevels.next, kt, link);
		koan_toplevel_destroy(kt);
	}

	wl_list_remove(&server->new_xdg_toplevel.link);
	wl_list_remove(&server->new_xdg_popup.link);
	wl_list_remove(&server->new_input.link);
	wl_list_remove(&server->new_surface.link);
	wl_list_remove(&server->new_toplevel_decoration.link);

	wl_list_remove(&server->keyboard_key.link);
	wl_list_remove(&server->keyboard_modifiers.link);
	if (server->seat)
		wlr_seat_destroy(server->seat);
	if (server->keyboard_ready) {
		wlr_keyboard_finish(&server->keyboard);
		server->keyboard_ready = false;
	}
	if (server->cursor)
		wlr_cursor_destroy(server->cursor);
	/* Destroy the output before its layout and the display: its destroy
	 * signal removes it from the layout and drops the wl_output global. */
	if (server->output)
		wlr_output_destroy(server->output);
	server->output = NULL;
	if (server->output_layout)
		wlr_output_layout_destroy(server->output_layout);
	if (server->scene)
		wlr_scene_node_destroy(&server->scene->tree.node);
	/* Go through the backend impl so events.destroy is emitted and the
	 * Koan backend frees itself (koan_backend_destroy). */
	if (server->backend)
		wlr_backend_destroy(server->backend);
	server->backend = NULL;
	server->koan_backend = NULL;

	if (server->queue_fd_source)
		wl_event_source_remove(server->queue_fd_source);

	/* Destroy the command queue. */
	if (server->queue)
		koan_queue_destroy(server->queue);
	server->queue = NULL;

	/* Destroy the Wayland display and all client resources. This cleans up
	 * wlroots globals (compositor, xdg-shell, shm, etc.). */
	wl_display_destroy_clients(display);
	wl_display_destroy(display);

	/* Free Koan-owned C objects after the display is gone. */
	if (server->allocator)
		wlr_allocator_destroy(server->allocator);
	if (server->renderer)
		wlr_renderer_destroy(server->renderer);

	memset(server, 0, sizeof(*server));
	wl_list_init(&server->toplevels);

	wlr_log(WLR_INFO, "Koan Wayland server: destroyed");
}

/* Run the Wayland event loop until terminated. Intended for standalone
 * testing or for the dedicated dispatch thread. */
void koan_server_run(struct wl_display *display) {
	if (!display)
		return;
	wlr_log(WLR_INFO, "Koan Wayland server: running event loop");
	wl_display_run(display);
}
