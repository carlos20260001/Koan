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
 * WLCS (github.com/canonical/wlcs) integration that tests a running
 * VitruvianOS session: app_server's real Wayland socket, window manager
 * and input dispatching. Built as koan-wlcs-vm-integration.so; run inside
 * the session (docker/wlcs/run-wlcs-vm.sh does it in the debug VM):
 *
 *   wlcs ./koan-wlcs-vm-integration.so [gtest options]
 *
 * Unlike koan-wlcs-integration.so, which runs the Koan protocol layer in
 * process with a stand-in for app_server, this module only connects:
 *   - each WLCS client is a new connection to $XDG_RUNTIME_DIR/
 *     $WAYLAND_DISPLAY (default wayland-0);
 *   - windows are placed and the pointer is driven through koan_test_v1
 *     (protocol/koan-test-v1.xml): app_server moves the native window and
 *     injects mouse events into its EventDispatcher, so stacking,
 *     hit-testing, click-to-focus and the per-window input path are the
 *     real ones;
 *   - every placement and pointer call ends with koan_test_v1.sync, which
 *     returns once the resulting Wayland events were sent.
 * The session must have been started with the root-owned
 * /run/koan-test-control present, or app_server offers no koan_test_v1.
 *
 * Touch: app_server has no touch input, so touch devices do nothing and
 * the report classifies those tests as NOT IMPLEMENTED.
 *
 * Licensing: GPL-3.0-only, like WLCS (debian/copyright: GPL-3; its
 * interface headers say "version 3"), whose headers this file implements
 * and whose runner loads it. The generated koan_test_v1 protocol code it
 * contains is MIT, which is compatible with that. It is a test artifact
 * and is not installed or packaged.
 */

#define _GNU_SOURCE
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include <wayland-client.h>

#include <wlcs/display_server.h>
#include <wlcs/pointer.h>
#include <wlcs/touch.h>

#include "koan-test-v1-client-protocol.h"

struct vm_server {
	WlcsDisplayServer base;
	struct sockaddr_un address;
	struct wl_display *control;      /* our own connection */
	struct koan_test_v1 *test;
	uint32_t buttons[8];             /* evdev codes held down, 0 = free */
};

/* The pointer outlives a test's server in app_server; relative moves
 * continue from the last absolute position. */
static double sPointerX, sPointerY;

static void
die(const char *what)
{
	fprintf(stderr, "koan-wlcs-vm: %s\n", what);
	abort();
}

/* ---- koan_test_v1 binding and sync ------------------------------------- */

static void
registry_global(void *data, struct wl_registry *registry, uint32_t name,
	const char *interface, uint32_t version)
{
	(void)version;
	struct koan_test_v1 **test = data;
	if (*test == NULL && strcmp(interface, koan_test_v1_interface.name) == 0)
		*test = wl_registry_bind(registry, name, &koan_test_v1_interface, 1);
}

static void
registry_global_remove(void *data, struct wl_registry *registry,
	uint32_t name)
{
	(void)data;
	(void)registry;
	(void)name;
}

static const struct wl_registry_listener sRegistryListener = {
	.global = registry_global,
	.global_remove = registry_global_remove,
};

static void
sync_done(void *data, struct wl_callback *callback, uint32_t serial)
{
	(void)serial;
	*(bool *)data = true;
	wl_callback_destroy(callback);
}

static const struct wl_callback_listener sSyncListener = {
	.done = sync_done,
};

/* Wait until app_server handled everything sent on test so far and the
 * Wayland events it caused were sent. queue NULL: the default queue. */
static void
test_sync(struct wl_display *display, struct wl_event_queue *queue,
	struct koan_test_v1 *test)
{
	bool done = false;
	struct wl_callback *callback = koan_test_v1_sync(test);
	wl_callback_add_listener(callback, &sSyncListener, &done);
	while (!done) {
		int r = queue != NULL ? wl_display_dispatch_queue(display, queue)
			: wl_display_dispatch(display);
		if (r < 0)
			die("connection lost while waiting for koan_test_v1.sync");
	}
}

static void
control_sync(struct vm_server *s)
{
	test_sync(s->control, NULL, s->test);
}

/* ---- pointer ----------------------------------------------------------- */

struct vm_pointer {
	WlcsPointer base;
	struct vm_server *server;
};

static void
pointer_move_to(struct vm_server *s, double x, double y)
{
	sPointerX = x;
	sPointerY = y;
	koan_test_v1_pointer_motion(s->test, wl_fixed_from_double(x),
		wl_fixed_from_double(y));
	control_sync(s);
}

static void
pointer_move_absolute(WlcsPointer *pointer, wl_fixed_t x, wl_fixed_t y)
{
	pointer_move_to(((struct vm_pointer *)pointer)->server,
		wl_fixed_to_double(x), wl_fixed_to_double(y));
}

static void
pointer_move_relative(WlcsPointer *pointer, wl_fixed_t dx, wl_fixed_t dy)
{
	pointer_move_to(((struct vm_pointer *)pointer)->server,
		sPointerX + wl_fixed_to_double(dx), sPointerY + wl_fixed_to_double(dy));
}

static void
pointer_button(struct vm_server *s, uint32_t button, bool pressed)
{
	size_t n = sizeof(s->buttons) / sizeof(s->buttons[0]);
	for (size_t i = 0; i < n; i++) {
		if (pressed && s->buttons[i] == 0) {
			s->buttons[i] = button;
			break;
		}
		if (!pressed && s->buttons[i] == button) {
			s->buttons[i] = 0;
			break;
		}
	}
	koan_test_v1_pointer_button(s->test, button, pressed
		? WL_POINTER_BUTTON_STATE_PRESSED : WL_POINTER_BUTTON_STATE_RELEASED);
	control_sync(s);
}

static void
pointer_button_down(WlcsPointer *pointer, int button)
{
	pointer_button(((struct vm_pointer *)pointer)->server, (uint32_t)button,
		true);
}

static void
pointer_button_up(WlcsPointer *pointer, int button)
{
	pointer_button(((struct vm_pointer *)pointer)->server, (uint32_t)button,
		false);
}

static void
pointer_destroy(WlcsPointer *pointer)
{
	free(pointer);
}

static WlcsPointer *
server_create_pointer(WlcsDisplayServer *server)
{
	struct vm_pointer *p = calloc(1, sizeof(*p));
	if (p == NULL)
		return NULL;
	p->base.version = WLCS_POINTER_VERSION;
	p->base.move_absolute = pointer_move_absolute;
	p->base.move_relative = pointer_move_relative;
	p->base.button_up = pointer_button_up;
	p->base.button_down = pointer_button_down;
	p->base.destroy = pointer_destroy;
	p->server = (struct vm_server *)server;
	return &p->base;
}

/* ---- touch: app_server has none ---------------------------------------- */

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

static int
server_create_client_socket(WlcsDisplayServer *server)
{
	struct vm_server *s = (struct vm_server *)server;
	int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
	if (fd < 0)
		return -1;
	if (connect(fd, (struct sockaddr *)&s->address, sizeof(s->address)) != 0) {
		close(fd);
		return -1;
	}
	return fd;
}

/* Sent on the client's own connection, after its surface requests, so
 * app_server sees the window the client just mapped. */
static void
server_position_window_absolute(WlcsDisplayServer *server,
	struct wl_display *client_display, struct wl_surface *client_surface,
	int x, int y)
{
	(void)server;
	struct wl_event_queue *queue = wl_display_create_queue(client_display);
	struct wl_display *wrapper = wl_proxy_create_wrapper(client_display);
	if (queue == NULL || wrapper == NULL)
		die("out of memory");
	wl_proxy_set_queue((struct wl_proxy *)wrapper, queue);
	struct wl_registry *registry = wl_display_get_registry(wrapper);
	struct koan_test_v1 *test = NULL;
	wl_registry_add_listener(registry, &sRegistryListener, &test);
	if (wl_display_roundtrip_queue(client_display, queue) < 0 || test == NULL)
		die("client connection has no koan_test_v1");

	koan_test_v1_move_surface(test, client_surface, x, y);
	test_sync(client_display, queue, test);

	koan_test_v1_destroy(test);
	wl_registry_destroy(registry);
	wl_proxy_wrapper_destroy(wrapper);
	wl_event_queue_destroy(queue);
}

/* What app_server advertises (wayland-info in the session). WLCS skips
 * tests for anything not listed. */
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

/* The server already runs; connect the control connection. The first sync
 * also lets app_server finish destroying the previous test's windows. */
static void
server_start(WlcsDisplayServer *server)
{
	struct vm_server *s = (struct vm_server *)server;
	int fd = server_create_client_socket(server);
	s->control = fd >= 0 ? wl_display_connect_to_fd(fd) : NULL;
	if (s->control == NULL)
		die("cannot connect to the session's Wayland socket");
	struct wl_registry *registry = wl_display_get_registry(s->control);
	wl_registry_add_listener(registry, &sRegistryListener, &s->test);
	if (wl_display_roundtrip(s->control) < 0 || s->test == NULL) {
		die("app_server offers no koan_test_v1: as root, create "
			"/run/koan-test-control and restart the session");
	}
	wl_registry_destroy(registry);
	control_sync(s);
}

static void
server_stop(WlcsDisplayServer *server)
{
	struct vm_server *s = (struct vm_server *)server;
	if (s->control == NULL)
		return;
	/* A failed test may leave buttons held; the next one must start with
	 * none. */
	for (size_t i = 0; i < sizeof(s->buttons) / sizeof(s->buttons[0]); i++) {
		if (s->buttons[i] != 0) {
			koan_test_v1_pointer_button(s->test, s->buttons[i],
				WL_POINTER_BUTTON_STATE_RELEASED);
			s->buttons[i] = 0;
		}
	}
	control_sync(s);
	koan_test_v1_destroy(s->test);
	s->test = NULL;
	wl_display_disconnect(s->control);
	s->control = NULL;
}

static WlcsDisplayServer *
create_server(int argc, const char **argv)
{
	(void)argc;
	(void)argv;
	const char *dir = getenv("XDG_RUNTIME_DIR");
	const char *name = getenv("WAYLAND_DISPLAY");
	if (dir == NULL)
		die("XDG_RUNTIME_DIR is not set");
	if (name == NULL || name[0] == '\0')
		name = "wayland-0";
	struct vm_server *s = calloc(1, sizeof(*s));
	if (s == NULL)
		return NULL;
	s->address.sun_family = AF_UNIX;
	if (snprintf(s->address.sun_path, sizeof(s->address.sun_path), "%s/%s",
			dir, name) >= (int)sizeof(s->address.sun_path))
		die("socket path too long");
	s->base.version = 3;
	s->base.start = server_start;
	s->base.stop = server_stop;
	s->base.create_client_socket = server_create_client_socket;
	s->base.position_window_absolute = server_position_window_absolute;
	s->base.create_pointer = server_create_pointer;
	s->base.create_touch = server_create_touch;
	s->base.get_descriptor = server_get_descriptor;
	s->base.start_on_this_thread = NULL;
	return &s->base;
}

static void
destroy_server(WlcsDisplayServer *server)
{
	server_stop(server);
	free(server);
}

const WlcsServerIntegration wlcs_server_integration = {
	.version = WLCS_SERVER_INTEGRATION_VERSION,
	.create_server = create_server,
	.destroy_server = destroy_server,
};
