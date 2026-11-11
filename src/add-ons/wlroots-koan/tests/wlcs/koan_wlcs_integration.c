/*
 * SPDX-License-Identifier: GPL-3.0-only
 * Copyright 2026, carlos20260001.
 *
 * This program is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 3,
 * as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * WLCS (Wayland Conformance Test Suite, github.com/canonical/wlcs)
 * integration for the Koan Wayland server.
 *
 * Built as koan-wlcs-integration.so and loaded by the wlcs runner:
 *
 *   wlcs ./koan-wlcs-integration.so [gtest options]
 *
 * The real Koan server (koan_server.c + koan_adapter.c over wlroots) runs
 * on WLCS's server thread through start_on_this_thread(), so every WLCS
 * call reaches this file on the Wayland thread and may touch wlroots
 * state directly.
 *
 * In VitruvianOS, app_server owns window placement, stacking, focus and
 * pointer hit-testing. WLCS runs without app_server, so this file stands in
 * for exactly those parts and nothing else:
 *   - each mapped toplevel gets a native-window record with a position
 *     (position_window_absolute) and a stacking order (newest on top,
 *     clicked window raised);
 *   - the absolute pointer is hit-tested against those records and sent to
 *     the window under it with koan_server_pointer_* in window-content
 *     coordinates, the same calls the app_server bridge makes;
 *   - a new window and a clicked window get keyboard focus and the
 *     activated state through the same queue commands app_server pushes
 *     (KOAN_CMD_KEYBOARD_FOCUS, KOAN_CMD_TOPLEVEL_CONFIGURE);
 *   - maximize/fullscreen requests are granted with the screen-sized frame,
 *     as app_server's zoom_window()/fullscreen_window() do.
 * Consequently WLCS results describe the Koan protocol layer, not
 * app_server's own window management.
 *
 * Licensing: GPL-3.0-only, like WLCS (debian/copyright: GPL-3; its
 * interface headers say "version 3"), whose headers this file implements
 * and whose runner loads it. The wlroots-koan code it links (MIT) is
 * compatible with that. It is a test artifact and is not installed or
 * packaged.
 */

#define _GNU_SOURCE
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#include <wayland-client-core.h>
#include <wayland-server-core.h>
#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_xdg_shell.h>
#include <wlr/util/log.h>

#include <wlcs/display_server.h>
#include <wlcs/pointer.h>
#include <wlcs/touch.h>

#include "koan_adapter.h"
#include "koan_queue.h"
#include "koan_server.h"

/* ---- native-window stand-in -------------------------------------------- */

struct window {
	struct wl_list link;            /* koan_wlcs_server.windows, top first */
	struct koan_wlcs_server *server;
	struct wlr_xdg_toplevel *toplevel;
	struct wl_listener toplevel_destroy;
	int32_t x, y;                   /* window-content top-left */
	bool visible;
	bool maximized, fullscreen;
	int32_t restore_x, restore_y;   /* position before max/fullscreen */
};

struct client_socket {
	struct wl_list link;
	int client_fd;                  /* the fd number WLCS connects with */
	struct wl_client *client;
	struct wl_listener destroy;
};

struct koan_wlcs_server {
	WlcsDisplayServer base;         /* must be first */
	struct wl_display *display;
	struct wl_event_source *wlcs_source;
	struct wl_list windows;
	struct wl_list sockets;
	struct window *pointer_window;  /* window under / grabbing the pointer */
	struct window *focus_window;    /* keyboard focus */
	double pointer_x, pointer_y;
	int buttons_down;
	int pointers;                   /* live WlcsPointer devices */
};

static struct koan_wlcs_server *sServer; /* WLCS runs one server at a time */

static uint32_t
now_ms(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (uint32_t)(ts.tv_sec * 1000 + ts.tv_nsec / 1000000);
}

/* The native window's content box in root-surface coordinates: exactly
 * what koan_adapter.c gives app_server (and maps pointer positions back
 * with), so the stand-in hit-tests the window app_server would show. */
static struct wlr_box
content_box(const struct window *w)
{
	return koan_adapter_window_box(w->toplevel);
}

/*
 * Where the content box starts on screen. WLCS (like Mir) places the
 * window geometry at the requested position when the client set one
 * explicitly; otherwise it places the root surface's origin there, and a
 * sub-surface at negative coordinates extends the window up/left of it.
 */
static void
content_origin(const struct window *w, double *x, double *y)
{
	const struct wlr_box *set = &w->toplevel->base->current.geometry;
	if (set->width > 0 && set->height > 0) {
		*x = w->x;
		*y = w->y;
		return;
	}
	struct wlr_box box = content_box(w);
	*x = w->x + box.x;
	*y = w->y + box.y;
}

static struct window *
window_at(struct koan_wlcs_server *s, double x, double y)
{
	struct window *w;
	wl_list_for_each(w, &s->windows, link) {
		if (!w->visible)
			continue;
		struct wlr_box box = content_box(w);
		double ox, oy;
		content_origin(w, &ox, &oy);
		if (x >= ox && y >= oy && x < ox + box.width && y < oy + box.height)
			return w;
	}
	return NULL;
}

static void pointer_update(struct koan_wlcs_server *s);

static void
push_focus(struct window *w, bool focused)
{
	union koan_cmd_payload focus = {0};
	focus.keyboard_focus.toplevel = w->toplevel;
	focus.keyboard_focus.focused = focused;
	koan_queue_push(koan_adapter_queue(), KOAN_CMD_KEYBOARD_FOCUS, &focus,
		NULL);

	union koan_cmd_payload configure = {0};
	configure.toplevel_configure.toplevel = w->toplevel;
	configure.toplevel_configure.width = 0;
	configure.toplevel_configure.height = 0;
	configure.toplevel_configure.maximized = -1;
	configure.toplevel_configure.activated = focused;
	koan_queue_push(koan_adapter_queue(), KOAN_CMD_TOPLEVEL_CONFIGURE,
		&configure, NULL);
}

/* Activate and raise, as app_server does for a new or clicked window. */
static void
activate(struct koan_wlcs_server *s, struct window *w)
{
	wl_list_remove(&w->link);
	wl_list_insert(&s->windows, &w->link);
	if (s->focus_window == w)
		return;
	if (s->focus_window != NULL)
		push_focus(s->focus_window, false);
	s->focus_window = w;
	push_focus(w, true);
}

static void
window_forget(struct window *w)
{
	struct koan_wlcs_server *s = w->server;
	if (s->pointer_window == w)
		s->pointer_window = NULL;
	if (s->focus_window == w)
		s->focus_window = NULL;
	wl_list_remove(&w->toplevel_destroy.link);
	wl_list_remove(&w->link);
	free(w);
}

static void
handle_toplevel_destroy(struct wl_listener *listener, void *data)
{
	(void)data;
	struct window *w = wl_container_of(listener, w, toplevel_destroy);
	window_forget(w);
}

static struct window *
find_window(struct koan_wlcs_server *s, struct wlr_xdg_toplevel *toplevel)
{
	struct window *w;
	wl_list_for_each(w, &s->windows, link) {
		if (w->toplevel == toplevel)
			return w;
	}
	return NULL;
}

/* koan_window_callbacks, on the Wayland thread. */

static void
cb_create_window(void *userdata, struct koan_queue *queue,
	struct wlr_xdg_toplevel *toplevel, int32_t width, int32_t height)
{
	(void)width;
	(void)height;
	struct koan_wlcs_server *s = userdata;
	struct window *w = calloc(1, sizeof(*w));
	if (w == NULL)
		return;
	w->server = s;
	w->toplevel = toplevel;
	w->visible = true;
	w->toplevel_destroy.notify = handle_toplevel_destroy;
	wl_signal_add(&toplevel->events.destroy, &w->toplevel_destroy);
	wl_list_insert(&s->windows, &w->link);

	/* The native window exists at once; tell the adapter. */
	union koan_cmd_payload ready = {0};
	ready.window_ready.toplevel = toplevel;
	ready.window_ready.window = w;
	koan_queue_push(queue, KOAN_CMD_WINDOW_READY, &ready, NULL);
	activate(s, w);
}

static void
cb_destroy_window(void *userdata, struct koan_queue *queue, void *window)
{
	(void)userdata;
	(void)queue;
	(void)window;
	/* The record goes away with the toplevel (handle_toplevel_destroy). */
}

static void
cb_set_title(void *userdata, struct koan_queue *queue, void *window,
	const char *title)
{
	(void)userdata;
	(void)queue;
	(void)window;
	(void)title;
}

static void
cb_set_size(void *userdata, struct koan_queue *queue, void *window,
	int32_t width, int32_t height)
{
	(void)userdata;
	(void)queue;
	(void)window;
	(void)width;
	(void)height;
}

static void
cb_set_buffer(void *userdata, struct koan_queue *queue, void *window,
	void *data, int32_t width, int32_t height, int32_t stride,
	uint32_t format, int32_t scale)
{
	(void)scale; /* 1: these tests never set an output scale */
	(void)userdata;
	(void)queue;
	(void)window;
	(void)width;
	(void)height;
	(void)stride;
	(void)format;
	free(data); /* set_buffer takes ownership */
	/* New content may move or resize what is under the pointer; app_server
	 * sends a fake mouse-moved in that case. */
	if (window != NULL)
		pointer_update(((struct window *)window)->server);
}

static void
cb_set_visibility(void *userdata, struct koan_queue *queue, void *window,
	bool visible)
{
	(void)userdata;
	(void)queue;
	struct window *w = window;
	if (w != NULL) {
		w->visible = visible;
		pointer_update(w->server);
	}
}

/* What app_server does for xdg_toplevel.set_/unset_maximized and
 * set_/unset_fullscreen (WaylandWindow.cpp: zoom_window() and
 * fullscreen_window()): grant it with the screen-sized frame at the screen
 * origin, and restore the previous position when the state is left. Koan's
 * output is 1920x1080 and the stand-in has no decorator to keep on screen. */
static void
cb_request_state(void *userdata, struct koan_queue *queue, void *window,
	int32_t maximized, int32_t fullscreen)
{
	(void)userdata;
	struct window *w = window;
	union koan_cmd_payload reply = {0};
	reply.toplevel_configure.toplevel = w->toplevel;
	reply.toplevel_configure.maximized = -1;
	reply.toplevel_configure.activated = -1;

	bool was_expanded = w->maximized || w->fullscreen;
	if (maximized >= 0 && !w->fullscreen)
		w->maximized = maximized != 0;
	if (fullscreen >= 0) {
		w->fullscreen = fullscreen != 0;
		reply.toplevel_configure.fullscreen = w->fullscreen
			? KOAN_STATE_ENTER : KOAN_STATE_LEAVE;
	}
	reply.toplevel_configure.maximized = w->maximized ? 1 : 0;

	bool expanded = w->maximized || w->fullscreen;
	if (expanded && !was_expanded) {
		w->restore_x = w->x;
		w->restore_y = w->y;
	}
	if (expanded) {
		w->x = 0;
		w->y = 0;
		reply.toplevel_configure.width = 1920;
		reply.toplevel_configure.height = 1080;
	} else if (was_expanded) {
		w->x = w->restore_x;
		w->y = w->restore_y;
	}
	koan_queue_push(queue, KOAN_CMD_TOPLEVEL_CONFIGURE, &reply, NULL);
}

static const struct koan_window_callbacks sCallbacks = {
	.create_window = cb_create_window,
	.destroy_window = cb_destroy_window,
	.set_title = cb_set_title,
	.set_size = cb_set_size,
	.set_buffer = cb_set_buffer,
	.set_visibility = cb_set_visibility,
	.request_state = cb_request_state,
};

/* ---- pointer ----------------------------------------------------------- */

struct koan_wlcs_pointer {
	WlcsPointer base;               /* must be first */
	struct koan_wlcs_server *server;
};

static void
pointer_update(struct koan_wlcs_server *s)
{
	if (s->pointers == 0)
		return; /* no pointer device yet: nothing to deliver */
	uint32_t time = now_ms();
	/* Implicit grab: while a button is held the pressed window keeps the
	 * pointer (app_server's mouse event window). */
	struct window *target = s->buttons_down > 0 ? s->pointer_window
		: window_at(s, s->pointer_x, s->pointer_y);

	if (s->pointer_window != NULL && s->pointer_window != target)
		koan_server_pointer_leave(s->pointer_window->toplevel);
	s->pointer_window = target;
	if (target != NULL) {
		/* Window-content coordinates, as app_server sends them. */
		double ox, oy;
		content_origin(target, &ox, &oy);
		koan_server_pointer_motion(target->toplevel, time,
			s->pointer_x - ox, s->pointer_y - oy);
	}
}

static void
pointer_move_absolute(WlcsPointer *pointer, wl_fixed_t x, wl_fixed_t y)
{
	struct koan_wlcs_server *s = ((struct koan_wlcs_pointer *)pointer)->server;
	s->pointer_x = wl_fixed_to_double(x);
	s->pointer_y = wl_fixed_to_double(y);
	pointer_update(s);
}

static void
pointer_move_relative(WlcsPointer *pointer, wl_fixed_t dx, wl_fixed_t dy)
{
	struct koan_wlcs_server *s = ((struct koan_wlcs_pointer *)pointer)->server;
	s->pointer_x += wl_fixed_to_double(dx);
	s->pointer_y += wl_fixed_to_double(dy);
	pointer_update(s);
}

static void
pointer_button(WlcsPointer *pointer, int button, bool pressed)
{
	struct koan_wlcs_server *s = ((struct koan_wlcs_pointer *)pointer)->server;
	struct window *w = s->pointer_window;
	if (pressed) {
		s->buttons_down++;
		if (w != NULL)
			activate(s, w);
	} else if (s->buttons_down > 0) {
		s->buttons_down--;
	}
	if (w != NULL)
		koan_server_pointer_button(w->toplevel, now_ms(), (uint32_t)button,
			pressed);
	if (!pressed && s->buttons_down == 0)
		pointer_update(s); /* grab ended: re-target */
}

static void
pointer_button_down(WlcsPointer *pointer, int button)
{
	pointer_button(pointer, button, true);
}

static void
pointer_button_up(WlcsPointer *pointer, int button)
{
	pointer_button(pointer, button, false);
}

static void
pointer_destroy(WlcsPointer *pointer)
{
	struct koan_wlcs_server *s = ((struct koan_wlcs_pointer *)pointer)->server;
	if (sServer == s && s->pointers > 0)
		s->pointers--;
	free(pointer);
}

static WlcsPointer *
server_create_pointer(WlcsDisplayServer *server)
{
	struct koan_wlcs_pointer *p = calloc(1, sizeof(*p));
	if (p == NULL)
		return NULL;
	p->base.version = WLCS_POINTER_VERSION;
	p->base.move_absolute = pointer_move_absolute;
	p->base.move_relative = pointer_move_relative;
	p->base.button_up = pointer_button_up;
	p->base.button_down = pointer_button_down;
	p->base.destroy = pointer_destroy;
	p->server = (struct koan_wlcs_server *)server;
	p->server->pointers++;
	return &p->base;
}

/* ---- touch: not implemented in Koan ------------------------------------ */

/* Koan's seat has no touch capability (app_server forwards no touch to
 * Wayland clients). These do nothing, so WLCS touch tests fail; the
 * report classifies them as NOT IMPLEMENTED. */
static void
touch_down(WlcsTouch *touch, wl_fixed_t x, wl_fixed_t y)
{
	(void)touch;
	(void)x;
	(void)y;
}

static void
touch_move(WlcsTouch *touch, wl_fixed_t x, wl_fixed_t y)
{
	(void)touch;
	(void)x;
	(void)y;
}

static void
touch_up(WlcsTouch *touch)
{
	(void)touch;
}

static void
touch_destroy(WlcsTouch *touch)
{
	free(touch);
}

static WlcsTouch *
server_create_touch(WlcsDisplayServer *server)
{
	(void)server;
	WlcsTouch *t = calloc(1, sizeof(*t));
	if (t == NULL)
		return NULL;
	t->version = WLCS_TOUCH_VERSION;
	t->touch_down = touch_down;
	t->touch_move = touch_move;
	t->touch_up = touch_up;
	t->destroy = touch_destroy;
	return t;
}

/* ---- clients and windows ----------------------------------------------- */

static void
handle_client_destroy(struct wl_listener *listener, void *data)
{
	(void)data;
	struct client_socket *cs = wl_container_of(listener, cs, destroy);
	wl_list_remove(&cs->destroy.link);
	wl_list_remove(&cs->link);
	free(cs);
}

static int
server_create_client_socket(WlcsDisplayServer *server)
{
	struct koan_wlcs_server *s = (struct koan_wlcs_server *)server;
	int fds[2];
	if (socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, fds) != 0)
		return -1;
	struct client_socket *cs = calloc(1, sizeof(*cs));
	struct wl_client *client = cs != NULL
		? wl_client_create(s->display, fds[0]) : NULL;
	if (client == NULL) {
		free(cs);
		close(fds[0]);
		close(fds[1]);
		return -1;
	}
	cs->client = client;
	cs->client_fd = fds[1];
	cs->destroy.notify = handle_client_destroy;
	wl_client_add_destroy_listener(client, &cs->destroy);
	wl_list_insert(&s->sockets, &cs->link);
	return fds[1];
}

static void
server_position_window_absolute(WlcsDisplayServer *server,
	struct wl_display *client_display, struct wl_surface *client_surface,
	int x, int y)
{
	struct koan_wlcs_server *s = (struct koan_wlcs_server *)server;

	/* The WLCS client connected with one of our socket fds; its server
	 * side wl_client owns the surface under the same object id. */
	int fd = wl_display_get_fd(client_display);
	struct client_socket *cs;
	struct wl_client *client = NULL;
	wl_list_for_each(cs, &s->sockets, link) {
		if (cs->client_fd == fd) {
			client = cs->client;
			break;
		}
	}
	if (client == NULL) {
		wlr_log(WLR_ERROR, "koan-wlcs: position for an unknown client");
		return;
	}
	struct wl_resource *resource = wl_client_get_object(client,
		wl_proxy_get_id((struct wl_proxy *)client_surface));
	if (resource == NULL)
		return;
	struct wlr_surface *surface = wlr_surface_from_resource(resource);
	struct wlr_xdg_toplevel *toplevel = wlr_xdg_toplevel_try_from_wlr_surface(
		wlr_surface_get_root_surface(surface));
	struct window *w = toplevel != NULL ? find_window(s, toplevel) : NULL;
	if (w == NULL) {
		wlr_log(WLR_ERROR, "koan-wlcs: position for a surface without a "
			"mapped toplevel");
		return;
	}
	w->x = x;
	w->y = y;
	pointer_update(s); /* the window may now be under the pointer */
}

/* Globals koan_server_create() advertises (checked by
 * koan_protocol_test core, registry-*). WLCS skips tests for anything not
 * listed here. */
static const WlcsExtensionDescriptor kExtensions[] = {
	{ "wl_compositor", 5 },
	{ "wl_subcompositor", 1 },
	{ "wl_shm", 2 },
	{ "wl_seat", 9 },
	{ "wl_output", 4 },
	{ "wl_data_device_manager", 3 },
	{ "xdg_wm_base", 3 },
	{ "zxdg_decoration_manager_v1", 1 },
	{ "org_kde_kwin_server_decoration_manager", 1 },
};

static const WlcsIntegrationDescriptor kDescriptor = {
	.version = WLCS_INTEGRATION_DESCRIPTOR_VERSION,
	.num_extensions = sizeof(kExtensions) / sizeof(kExtensions[0]),
	.supported_extensions = kExtensions,
};

static const WlcsIntegrationDescriptor *
server_get_descriptor(const WlcsDisplayServer *server)
{
	(void)server;
	return &kDescriptor;
}

/* ---- lifecycle --------------------------------------------------------- */

static int
dispatch_wlcs(int fd, uint32_t mask, void *data)
{
	(void)fd;
	(void)mask;
	wl_event_loop_dispatch(data, 0);
	return 0;
}

static void
server_start_on_this_thread(WlcsDisplayServer *server,
	struct wl_event_loop *wlcs_loop)
{
	struct koan_wlcs_server *s = (struct koan_wlcs_server *)server;

	/* Same order as app_server: callbacks, create, start, run. */
	koan_adapter_set_window_callbacks(&sCallbacks, NULL, s);
	s->display = koan_server_create();
	if (s->display == NULL || !koan_server_start()) {
		wlr_log(WLR_ERROR, "koan-wlcs: Koan server failed to start");
		abort();
	}
	s->wlcs_source = wl_event_loop_add_fd(
		koan_server_get_event_loop(s->display),
		wl_event_loop_get_fd(wlcs_loop), WL_EVENT_READABLE, dispatch_wlcs,
		wlcs_loop);

	koan_server_run(s->display); /* until stop() terminates it */

	wl_event_source_remove(s->wlcs_source);
	s->wlcs_source = NULL;
	while (!wl_list_empty(&s->windows)) {
		struct window *w = wl_container_of(s->windows.next, w, link);
		window_forget(w);
	}
	koan_server_destroy(s->display); /* also destroys the clients */
	s->display = NULL;
}

static void
server_stop(WlcsDisplayServer *server)
{
	/* Called on the Wayland thread (proxied by WLCS). */
	struct koan_wlcs_server *s = (struct koan_wlcs_server *)server;
	if (s->display != NULL)
		wl_display_terminate(s->display);
}

static WlcsDisplayServer *
create_server(int argc, const char **argv)
{
	(void)argc;
	(void)argv;
	if (sServer != NULL)
		return NULL; /* Koan's server is a process-wide singleton */
	wlr_log_init(WLR_ERROR, NULL);
	struct koan_wlcs_server *s = calloc(1, sizeof(*s));
	if (s == NULL)
		return NULL;
	s->base.version = 3;
	s->base.start = NULL;           /* start_on_this_thread is used */
	s->base.stop = server_stop;
	s->base.create_client_socket = server_create_client_socket;
	s->base.position_window_absolute = server_position_window_absolute;
	s->base.create_pointer = server_create_pointer;
	s->base.create_touch = server_create_touch;
	s->base.get_descriptor = server_get_descriptor;
	s->base.start_on_this_thread = server_start_on_this_thread;
	wl_list_init(&s->windows);
	wl_list_init(&s->sockets);
	/* app_server starts with the cursor in the middle of the screen
	 * (Desktop::Init, MoveCursorTo(width / 2, height / 2)); Koan's output
	 * is 1920x1080 (koan_server_create_output). */
	s->pointer_x = 1920 / 2;
	s->pointer_y = 1080 / 2;
	sServer = s;
	return &s->base;
}

static void
destroy_server(WlcsDisplayServer *server)
{
	struct koan_wlcs_server *s = (struct koan_wlcs_server *)server;
	if (sServer == s)
		sServer = NULL;
	free(s);
}

const WlcsServerIntegration wlcs_server_integration = {
	.version = WLCS_SERVER_INTEGRATION_VERSION,
	.create_server = create_server,
	.destroy_server = destroy_server,
};
