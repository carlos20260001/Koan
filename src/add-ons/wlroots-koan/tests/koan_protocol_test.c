/*
 * SPDX-License-Identifier: MIT
 *
 * Copyright © 2012 Intel Corporation
 * Copyright © 2012, 2013, 2014 Collabora, Ltd.
 * Copyright © 2026 carlos20260001
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial
 * portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT.  IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

/*
 * Derived work: the cases marked "Weston ..." below reproduce test cases
 * from the Weston test suite (vendor/weston, 16.0.90, c2772abe70ea):
 *
 *   tests/bad-buffer-test.c  Copyright © 2012 Intel Corporation,
 *                            Copyright © 2013 Collabora, Ltd.
 *   tests/roles-test.c       Copyright © 2014 Collabora, Ltd.
 *   tests/subsurface-test.c  Copyright © 2012 Collabora, Ltd.
 *
 * all under the MIT (Expat) license above, Weston's license (its
 * COPYING: "the version of the MIT 'Expat' License used by X.org"), with
 * the same wording as those files. They are rewritten as plain libwayland
 * clients for the Koan server; no Weston harness code is used. The
 * remaining code is Koan's own and is offered under the same terms.
 */

/*
 * Wayland protocol smoke, registry and negative tests against the real Koan
 * server (Milestone 1 of docs/analysis/wayland-conformance-testing-audit.md).
 *
 * The server runs in-process on its own thread, as in app_server; clients
 * are plain libwayland clients. Each case prints one machine-readable line:
 *
 *   RESULT|<PASS|FAIL|SKIPPED|NOT_IMPLEMENTED|BLOCKED>|<case>|<detail>
 *
 * and the run ends with a SUMMARY line. Only PASS is a pass; SKIPPED and
 * NOT_IMPLEMENTED are counted apart. The exit status is 1 if any case
 * FAILED or was BLOCKED.
 *
 * Usage: koan_protocol_test
 *   [core|lifecycle|bad-buffer|subsurface|compose|xdg|data|input|testctl]
 *
 *   core        registry inventory and invalid-request handling: the
 *               offending client gets the right protocol error, a second
 *               ("witness") client stays connected, the server keeps
 *               accepting clients. Cases ported from Weston's
 *               roles-test.c and subsurface-test.c use only standard
 *               protocols (vendor/weston/tests).
 *   lifecycle   create -> serve -> destroy the server twice in one process
 *               (the WLCS model creates a server per test).
 *   bad-buffer  Weston's bad-buffer-test: a mapped toplevel commits an shm
 *               buffer whose file was truncated. wlroots replaces the
 *               mapping with zeroes instead of posting an error (unlike
 *               Weston), so the requirement here is that the server and
 *               other clients survive.
 *   subsurface  valid sub-surface request sequences from Weston's
 *               subsurface-test.c (no error expected), including every
 *               3-step destroy permutation of an 11-surface tree.
 *   compose     what reaches app_server: a 60-frame callback loop, nested
 *               effective synchronization, sub-surface position applied on
 *               the parent's commit, parent destroyed with pending state.
 *   xdg         xdg_popup: initial configure, positioner geometry, map,
 *               destroy. Display is NOT_IMPLEMENTED (no bridge yet).
 *   data        clipboard selection between two clients: offered to the
 *               client that gets keyboard focus, delivered to the focused
 *               client, ignored with an invalid serial.
 *   input       pointer coordinates from app_server map to the surface
 *               the native window shows, also after a sub-surface that
 *               widened the window unmaps.
 *
 * lifecycle and bad-buffer run in their own process (separate ctest
 * entries) so a crash is attributed to them alone.
 */

#define _GNU_SOURCE
#include <errno.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#include <wayland-client.h>
#include <wayland-server-core.h>
#include <wlr/util/log.h>

#include "xdg-shell-client-protocol.h"
#include "viewporter-client-protocol.h"
#include "fractional-scale-v1-client-protocol.h"
#include "koan-test-v1-client-protocol.h"
#include "koan_test.h"

#include "koan_adapter.h"
#include "koan_queue.h"
#include "koan_server.h"

/* ---- results ----------------------------------------------------------- */

enum status {
	ST_PASS,
	ST_FAIL,
	ST_SKIPPED,
	ST_NOT_IMPLEMENTED,
	ST_BLOCKED,
	ST_COUNT
};

static const char *const kStatusNames[ST_COUNT] = {
	"PASS", "FAIL", "SKIPPED", "NOT_IMPLEMENTED", "BLOCKED"
};
static int sCounts[ST_COUNT];

static void
result(enum status status, const char *name, const char *fmt, ...)
{
	char detail[512] = "";
	if (fmt != NULL) {
		va_list args;
		va_start(args, fmt);
		vsnprintf(detail, sizeof(detail), fmt, args);
		va_end(args);
	}
	sCounts[status]++;
	printf("RESULT|%s|%s|%s\n", kStatusNames[status], name, detail);
	fflush(stdout);
}

static int
summary(void)
{
	printf("SUMMARY|pass=%d|fail=%d|skipped=%d|not_implemented=%d|"
		"blocked=%d\n", sCounts[ST_PASS], sCounts[ST_FAIL],
		sCounts[ST_SKIPPED], sCounts[ST_NOT_IMPLEMENTED],
		sCounts[ST_BLOCKED]);
	return sCounts[ST_FAIL] + sCounts[ST_BLOCKED] > 0 ? 1 : 0;
}

/* ---- fake app_server --------------------------------------------------- */

static _Atomic(struct wlr_xdg_toplevel *) sCreatedToplevel;
static atomic_int sSetBufferCount;
static int sNativeWindowToken;
static pthread_mutex_t sFrameLock = PTHREAD_MUTEX_INITIALIZER;
static void *sFrame;
static int32_t sFrameWidth;
static int32_t sFrameHeight;
static int32_t sFrameStride;
static float sFrameScale;

static void
fake_create_window(void *userdata, struct koan_queue *queue,
	struct wlr_xdg_toplevel *toplevel, int32_t width, int32_t height)
{
	(void)userdata;
	(void)queue;
	(void)width;
	(void)height;
	atomic_store(&sCreatedToplevel, toplevel);
}

/* Native windows app_server was told to destroy, and the last one. */
static atomic_int sDestroyWindowCount;
static _Atomic(void *) sDestroyedWindow;

static void
fake_destroy_window(void *userdata, struct koan_queue *queue, void *window)
{
	(void)userdata;
	(void)queue;
	atomic_store(&sDestroyedWindow, window);
	atomic_fetch_add(&sDestroyWindowCount, 1);
}

static void
fake_set_title(void *userdata, struct koan_queue *queue, void *window,
	const char *title)
{
	(void)userdata;
	(void)queue;
	(void)window;
	(void)title;
}

static void
fake_set_size(void *userdata, struct koan_queue *queue, void *window,
	int32_t width, int32_t height)
{
	(void)userdata;
	(void)queue;
	(void)window;
	(void)width;
	(void)height;
}

static void
fake_set_buffer(void *userdata, struct koan_queue *queue, void *window,
	void *data, int32_t width, int32_t height, int32_t stride,
	uint32_t format, float scale)
{
	(void)userdata;
	(void)queue;
	(void)window;
	(void)format;
	/* Keep the last delivered frame: what app_server would show. */
	pthread_mutex_lock(&sFrameLock);
	free(sFrame);
	sFrame = data; /* set_buffer takes ownership */
	sFrameWidth = width;
	sFrameHeight = height;
	sFrameStride = stride;
	sFrameScale = scale;
	pthread_mutex_unlock(&sFrameLock);
	atomic_fetch_add(&sSetBufferCount, 1);
}

/* Pixel (x, y) of the last frame delivered to app_server, 0 if none. */
static uint32_t
frame_pixel(int32_t x, int32_t y)
{
	uint32_t pixel = 0;
	pthread_mutex_lock(&sFrameLock);
	if (sFrame != NULL && x >= 0 && y >= 0 && x < sFrameWidth
		&& y < sFrameHeight) {
		pixel = ((const uint32_t *)((const uint8_t *)sFrame
			+ (size_t)y * sFrameStride))[x];
	}
	pthread_mutex_unlock(&sFrameLock);
	return pixel;
}

static void
fake_set_visibility(void *userdata, struct koan_queue *queue, void *window,
	bool visible)
{
	(void)userdata;
	(void)queue;
	(void)window;
	(void)visible;
}

/* app_server's answer to a state request: grant it, with the screen-sized
 * frame zoom_window() / fullscreen use (here a fixed 800x600 screen). */
static atomic_int sStateRequests;

static void
fake_request_state(void *userdata, struct koan_queue *queue, void *window,
	int32_t maximized, int32_t fullscreen)
{
	(void)userdata;
	(void)window;
	atomic_fetch_add(&sStateRequests, 1);
	union koan_cmd_payload reply = {0};
	reply.toplevel_configure.toplevel = atomic_load(&sCreatedToplevel);
	reply.toplevel_configure.maximized = maximized;
	reply.toplevel_configure.activated = -1;
	if (fullscreen >= 0) {
		reply.toplevel_configure.fullscreen = fullscreen
			? KOAN_STATE_ENTER : KOAN_STATE_LEAVE;
	}
	if (maximized == 1 || fullscreen == 1) {
		reply.toplevel_configure.width = 800;
		reply.toplevel_configure.height = 600;
	}
	koan_queue_push(queue, KOAN_CMD_TOPLEVEL_CONFIGURE, &reply, NULL);
}

/* app_server's minimize: counted, nothing else (no reply in xdg-shell). */
static atomic_int sMinimizeRequests;

static void
fake_request_minimize(void *userdata, struct koan_queue *queue, void *window)
{
	(void)userdata;
	(void)queue;
	(void)window;
	atomic_fetch_add(&sMinimizeRequests, 1);
}

/* testctl mode: create the koan_test_v1 global in server_start(). */
static bool sTestControl;

/* app_server's popup windows: the last create_popup request. */
static pthread_mutex_t sPopupLock = PTHREAD_MUTEX_INITIALIZER;
static struct {
	int creates;
	int moves;
	struct wlr_xdg_popup *popup;
	struct wlr_xdg_toplevel *toplevel;
	int32_t x, y, width, height;
} sPopupWm;

static void
fake_create_popup(void *userdata, struct koan_queue *queue,
	struct wlr_xdg_popup *popup, struct wlr_xdg_toplevel *toplevel,
	int32_t x, int32_t y, int32_t width, int32_t height)
{
	(void)userdata;
	(void)queue;
	pthread_mutex_lock(&sPopupLock);
	sPopupWm.creates++;
	sPopupWm.popup = popup;
	sPopupWm.toplevel = toplevel;
	sPopupWm.x = x;
	sPopupWm.y = y;
	sPopupWm.width = width;
	sPopupWm.height = height;
	pthread_mutex_unlock(&sPopupLock);
}

static void
fake_move_popup(void *userdata, struct koan_queue *queue, void *window,
	int32_t x, int32_t y)
{
	(void)userdata;
	(void)queue;
	(void)window;
	pthread_mutex_lock(&sPopupLock);
	sPopupWm.moves++;
	sPopupWm.x = x;
	sPopupWm.y = y;
	pthread_mutex_unlock(&sPopupLock);
}

static struct koan_window_callbacks sCallbacks = {
	.create_window = fake_create_window,
	.destroy_window = fake_destroy_window,
	.set_title = fake_set_title,
	.set_size = fake_set_size,
	.set_buffer = fake_set_buffer,
	.set_visibility = fake_set_visibility,
	.request_state = fake_request_state,
	.request_minimize = fake_request_minimize,
	.create_popup = fake_create_popup,
	.move_popup = fake_move_popup,
};

/* ---- server ------------------------------------------------------------ */

struct server {
	struct wl_display *display;
	const char *socket;
	pthread_t thread;
	bool running;
};

static void *
server_thread(void *data)
{
	koan_server_run(data);
	return NULL;
}

static bool
server_start(struct server *s)
{
	memset(s, 0, sizeof(*s));
	/* Same order as app_server: callbacks, create, socket, start, run. */
	koan_adapter_set_window_callbacks(&sCallbacks, NULL,
		&sNativeWindowToken);
	s->display = koan_server_create();
	if (s->display == NULL)
		return false;
	/* app_server creates it when /run/koan-test-control is present. */
	if (sTestControl && !koan_test_control_create(s->display))
		return false;
	s->socket = koan_server_add_socket(s->display);
	if (s->socket == NULL || !koan_server_start())
		return false;
	if (pthread_create(&s->thread, NULL, server_thread, s->display) != 0)
		return false;
	s->running = true;
	return true;
}

static void
server_stop(struct server *s)
{
	/* Same teardown as WaylandWindowBridge::Shutdown(). */
	if (s->running) {
		wl_display_terminate(s->display);
		pthread_join(s->thread, NULL);
	}
	if (s->display != NULL)
		koan_server_destroy(s->display);
	memset(s, 0, sizeof(*s));
}

/* ---- client ------------------------------------------------------------ */

#define MAX_GLOBALS 64

struct global {
	char interface[64];
	uint32_t name;
	uint32_t version;
};

struct conn {
	struct wl_display *display;
	struct wl_registry *registry;
	struct wl_compositor *compositor;
	struct wl_subcompositor *subcompositor;
	struct wl_shm *shm;
	struct xdg_wm_base *wm_base;
	struct wl_seat *seat;
	struct wl_output *output;   /* bound so wl_surface.enter can name it */
	struct wl_data_device_manager *data_device_manager;
	struct wp_viewporter *viewporter;
	struct wp_fractional_scale_manager_v1 *fractional_scale_manager;
	uint32_t seat_caps;
	struct global globals[MAX_GLOBALS];
	int global_count;
};

static void
seat_capabilities(void *data, struct wl_seat *seat, uint32_t caps)
{
	(void)seat;
	((struct conn *)data)->seat_caps = caps;
}

static void
seat_name(void *data, struct wl_seat *seat, const char *name)
{
	(void)data;
	(void)seat;
	(void)name;
}

static const struct wl_seat_listener sSeatListener = {
	.capabilities = seat_capabilities,
	.name = seat_name,
};

static void
wm_base_ping(void *data, struct xdg_wm_base *wm_base, uint32_t serial)
{
	(void)data;
	xdg_wm_base_pong(wm_base, serial);
}

static const struct xdg_wm_base_listener sWmBaseListener = {
	.ping = wm_base_ping,
};

static uint32_t
min_u32(uint32_t a, uint32_t b)
{
	return a < b ? a : b;
}

static void
registry_global(void *data, struct wl_registry *registry, uint32_t name,
	const char *interface, uint32_t version)
{
	struct conn *c = data;
	if (c->global_count < MAX_GLOBALS) {
		struct global *g = &c->globals[c->global_count++];
		snprintf(g->interface, sizeof(g->interface), "%s", interface);
		g->name = name;
		g->version = version;
	}

	/* Bind the first instance of each interface the cases need. */
	if (strcmp(interface, wl_compositor_interface.name) == 0
		&& c->compositor == NULL) {
		c->compositor = wl_registry_bind(registry, name,
			&wl_compositor_interface, min_u32(version, 4));
	} else if (strcmp(interface, wl_subcompositor_interface.name) == 0
		&& c->subcompositor == NULL) {
		c->subcompositor = wl_registry_bind(registry, name,
			&wl_subcompositor_interface, 1);
	} else if (strcmp(interface, wl_shm_interface.name) == 0
		&& c->shm == NULL) {
		c->shm = wl_registry_bind(registry, name, &wl_shm_interface, 1);
	} else if (strcmp(interface, xdg_wm_base_interface.name) == 0
		&& c->wm_base == NULL) {
		c->wm_base = wl_registry_bind(registry, name,
			&xdg_wm_base_interface, 1);
		xdg_wm_base_add_listener(c->wm_base, &sWmBaseListener, NULL);
	} else if (strcmp(interface, wl_data_device_manager_interface.name) == 0
		&& c->data_device_manager == NULL) {
		c->data_device_manager = wl_registry_bind(registry, name,
			&wl_data_device_manager_interface, min_u32(version, 3));
	} else if (strcmp(interface, wl_output_interface.name) == 0
		&& c->output == NULL) {
		c->output = wl_registry_bind(registry, name, &wl_output_interface,
			min_u32(version, 2));
	} else if (strcmp(interface,
			wp_fractional_scale_manager_v1_interface.name) == 0
		&& c->fractional_scale_manager == NULL) {
		c->fractional_scale_manager = wl_registry_bind(registry, name,
			&wp_fractional_scale_manager_v1_interface, 1);
	} else if (strcmp(interface, wp_viewporter_interface.name) == 0
		&& c->viewporter == NULL) {
		c->viewporter = wl_registry_bind(registry, name,
			&wp_viewporter_interface, 1);
	} else if (strcmp(interface, wl_seat_interface.name) == 0
		&& c->seat == NULL) {
		c->seat = wl_registry_bind(registry, name, &wl_seat_interface,
			min_u32(version, 5));
		wl_seat_add_listener(c->seat, &sSeatListener, c);
	}
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

static bool
settle(struct conn *c)
{
	for (int i = 0; i < 4; i++) {
		if (wl_display_roundtrip(c->display) < 0)
			return false;
	}
	return true;
}

static bool
conn_open(struct conn *c, const char *socket)
{
	memset(c, 0, sizeof(*c));
	c->display = wl_display_connect(socket);
	if (c->display == NULL)
		return false;
	c->registry = wl_display_get_registry(c->display);
	wl_registry_add_listener(c->registry, &sRegistryListener, c);
	return settle(c);
}

static void
conn_close(struct conn *c)
{
	/* Disconnecting tears down every client-side proxy with it. */
	if (c->display != NULL)
		wl_display_disconnect(c->display);
	memset(c, 0, sizeof(*c));
}

static const struct global *
find_global(const struct conn *c, const char *interface, int *count)
{
	const struct global *first = NULL;
	*count = 0;
	for (int i = 0; i < c->global_count; i++) {
		if (strcmp(c->globals[i].interface, interface) == 0) {
			if (first == NULL)
				first = &c->globals[i];
			(*count)++;
		}
	}
	return first;
}

static int
make_shm_fd(size_t size)
{
	int fd = memfd_create("koan-protocol-test", MFD_CLOEXEC);
	if (fd < 0)
		return -1;
	if (ftruncate(fd, (off_t)size) != 0) {
		close(fd);
		return -1;
	}
	return fd;
}

/* libwayland-client reports an error on an object the client already
 * destroyed without its interface ("[destroyed object]"). */
static const char kDestroyedObject[] = "(destroyed object)";

/*
 * Flushes the client's requests and checks that the server answered with
 * exactly this protocol error. Returns true on match; describes the
 * outcome in detail either way.
 */
static bool
expect_protocol_error(struct conn *c, const char *interface, uint32_t code,
	char *detail, size_t size)
{
	bool died = false;
	for (int i = 0; i < 4 && !died; i++)
		died = wl_display_roundtrip(c->display) < 0;
	if (!died) {
		snprintf(detail, size, "no protocol error; expected %s#%u",
			interface, code);
		return false;
	}

	int error = wl_display_get_error(c->display);
	if (error != EPROTO) {
		snprintf(detail, size, "connection failed with errno %d, not a "
			"protocol error; expected %s#%u", error, interface, code);
		return false;
	}

	const struct wl_interface *got = NULL;
	uint32_t id = 0;
	uint32_t got_code = wl_display_get_protocol_error(c->display, &got, &id);
	const char *got_name = got != NULL ? got->name : kDestroyedObject;
	snprintf(detail, size, "got %s#%u on object %u; expected %s#%u",
		got_name, got_code, id, interface, code);
	return strcmp(got_name, interface) == 0 && got_code == code;
}

/* ---- core: registry ---------------------------------------------------- */

static const char *const kCoreGlobals[] = {
	"wl_compositor",
	"wl_subcompositor",
	"wl_shm",
	"wl_seat",
	"wl_output",
	"wl_data_device_manager",
	"xdg_wm_base",
};

/* Protocols the audit lists as not implemented yet. */
static const char *const kNotImplementedGlobals[] = {
	"wp_presentation",
	"zwp_input_timestamps_manager_v1",
	"zwp_text_input_manager_v3",
};

static void
test_registry(const char *socket)
{
	struct conn c;
	if (!conn_open(&c, socket)) {
		result(ST_BLOCKED, "registry-roundtrip", "could not connect");
		return;
	}
	result(ST_PASS, "registry-roundtrip", "%d globals", c.global_count);

	char missing[256] = "";
	for (size_t i = 0; i < sizeof(kCoreGlobals) / sizeof(*kCoreGlobals);
		i++) {
		int count;
		if (find_global(&c, kCoreGlobals[i], &count) == NULL) {
			strncat(missing, kCoreGlobals[i],
				sizeof(missing) - strlen(missing) - 2);
			strcat(missing, " ");
		}
	}
	result(missing[0] == '\0' ? ST_PASS : ST_FAIL, "registry-core-globals",
		missing[0] == '\0' ? "all present" : "missing: %s", missing);

	/* A singleton global advertised twice makes clients bind an
	 * arbitrary one; every interface Koan offers must appear once. */
	char duplicates[256] = "";
	for (int i = 0; i < c.global_count; i++) {
		int count;
		const struct global *first = find_global(&c,
			c.globals[i].interface, &count);
		if (count > 1 && first == &c.globals[i]) {
			char item[96];
			snprintf(item, sizeof(item), "%s x%d ", c.globals[i].interface,
				count);
			strncat(duplicates, item,
				sizeof(duplicates) - strlen(duplicates) - 1);
		}
	}
	result(duplicates[0] == '\0' ? ST_PASS : ST_FAIL,
		"registry-globals-unique",
		duplicates[0] == '\0' ? "each interface once" : "duplicated: %s",
		duplicates);

	for (int i = 0; i < c.global_count; i++) {
		printf("INFO|global|%s|v%u\n", c.globals[i].interface,
			c.globals[i].version);
	}

	for (size_t i = 0; i < sizeof(kNotImplementedGlobals)
		/ sizeof(*kNotImplementedGlobals); i++) {
		int count;
		char name[96];
		snprintf(name, sizeof(name), "protocol-%s",
			kNotImplementedGlobals[i]);
		if (find_global(&c, kNotImplementedGlobals[i], &count) == NULL) {
			result(ST_NOT_IMPLEMENTED, name, "global not advertised");
		} else {
			result(ST_SKIPPED, name,
				"advertised but no conformance case yet");
		}
	}

	if (c.seat == NULL) {
		result(ST_FAIL, "seat-touch", "no wl_seat");
	} else if ((c.seat_caps & WL_SEAT_CAPABILITY_TOUCH) == 0) {
		result(ST_NOT_IMPLEMENTED, "seat-touch",
			"seat has no touch capability (caps 0x%x)", c.seat_caps);
	} else {
		result(ST_SKIPPED, "seat-touch",
			"touch advertised but no conformance case yet");
	}

	conn_close(&c);
}

/* ---- core: negative cases ---------------------------------------------- */

typedef void (*bad_request)(struct conn *c);

static void
bad_bind_version(struct conn *c)
{
	int count;
	const struct global *g = find_global(c, "wl_compositor", &count);
	if (g != NULL) {
		wl_registry_bind(c->registry, g->name, &wl_compositor_interface,
			g->version + 1);
	}
}

static void
bad_bind_unknown_name(struct conn *c)
{
	wl_registry_bind(c->registry, 0x7ffffff0u, &wl_compositor_interface, 1);
}

static void
bad_shm_pool_size(struct conn *c)
{
	int fd = make_shm_fd(4096);
	wl_shm_create_pool(c->shm, fd, 0);
	close(fd);
}

static void
bad_shm_buffer_outside_pool(struct conn *c)
{
	int fd = make_shm_fd(64 * 64 * 4);
	struct wl_shm_pool *pool = wl_shm_create_pool(c->shm, fd, 64 * 64 * 4);
	close(fd);
	wl_shm_pool_create_buffer(pool, 0, 64, 65, 64 * 4,
		WL_SHM_FORMAT_ARGB8888);
}

static void
bad_shm_buffer_format(struct conn *c)
{
	int fd = make_shm_fd(16 * 16 * 4);
	struct wl_shm_pool *pool = wl_shm_create_pool(c->shm, fd, 16 * 16 * 4);
	close(fd);
	wl_shm_pool_create_buffer(pool, 0, 16, 16, 16 * 4, 0x7a7a7a7au);
}

static void
bad_shm_pool_shrink(struct conn *c)
{
	int fd = make_shm_fd(8192);
	struct wl_shm_pool *pool = wl_shm_create_pool(c->shm, fd, 8192);
	close(fd);
	wl_shm_pool_resize(pool, 4096);
}

/* Weston roles-test.c: test_role_conflict_sub_wlshell. */
static void
bad_role_subsurface_then_xdg(struct conn *c)
{
	struct wl_surface *parent = wl_compositor_create_surface(c->compositor);
	struct wl_surface *child = wl_compositor_create_surface(c->compositor);
	wl_subcompositor_get_subsurface(c->subcompositor, child, parent);
	xdg_wm_base_get_xdg_surface(c->wm_base, child);
}

/* Weston roles-test.c: test_role_conflict_wlshell_sub. */
static void
bad_role_toplevel_then_subsurface(struct conn *c)
{
	struct wl_surface *parent = wl_compositor_create_surface(c->compositor);
	struct wl_surface *child = wl_compositor_create_surface(c->compositor);
	struct xdg_surface *xdg = xdg_wm_base_get_xdg_surface(c->wm_base, child);
	xdg_surface_get_toplevel(xdg);
	wl_subcompositor_get_subsurface(c->subcompositor, child, parent);
}

/* Weston subsurface-test.c: test_subsurface_paradox. */
static void
bad_subsurface_own_parent(struct conn *c)
{
	struct wl_surface *surface = wl_compositor_create_surface(c->compositor);
	wl_subcompositor_get_subsurface(c->subcompositor, surface, surface);
}

/* Weston subsurface-test.c: test_subsurface_identical_link. */
static void
bad_subsurface_identical_link(struct conn *c)
{
	struct wl_surface *parent = wl_compositor_create_surface(c->compositor);
	struct wl_surface *child = wl_compositor_create_surface(c->compositor);
	wl_subcompositor_get_subsurface(c->subcompositor, child, parent);
	wl_subcompositor_get_subsurface(c->subcompositor, child, parent);
}

/* Weston subsurface-test.c: test_subsurface_change_link. */
static void
bad_subsurface_change_link(struct conn *c)
{
	struct wl_surface *parent = wl_compositor_create_surface(c->compositor);
	struct wl_surface *stranger = wl_compositor_create_surface(c->compositor);
	struct wl_surface *child = wl_compositor_create_surface(c->compositor);
	wl_subcompositor_get_subsurface(c->subcompositor, child, parent);
	wl_subcompositor_get_subsurface(c->subcompositor, child, stranger);
}

/* Weston subsurface-test.c: test_subsurface_loop_paradox. */
static void
bad_subsurface_loop(struct conn *c)
{
	struct wl_surface *s[3];
	for (int i = 0; i < 3; i++)
		s[i] = wl_compositor_create_surface(c->compositor);
	wl_subcompositor_get_subsurface(c->subcompositor, s[1], s[0]);
	wl_subcompositor_get_subsurface(c->subcompositor, s[2], s[1]);
	wl_subcompositor_get_subsurface(c->subcompositor, s[0], s[2]);
}

/* Weston subsurface-test.c: test_subsurface_place_above_stranger. */
static void
bad_subsurface_place_above_stranger(struct conn *c)
{
	struct wl_surface *parent = wl_compositor_create_surface(c->compositor);
	struct wl_surface *child = wl_compositor_create_surface(c->compositor);
	struct wl_surface *stranger = wl_compositor_create_surface(c->compositor);
	struct wl_subsurface *sub = wl_subcompositor_get_subsurface(
		c->subcompositor, child, parent);
	wl_subsurface_place_above(sub, stranger);
}

/* Weston subsurface-test.c: test_subsurface_place_below_foreign. */
static void
bad_subsurface_place_below_foreign(struct conn *c)
{
	struct wl_surface *parent1 = wl_compositor_create_surface(c->compositor);
	struct wl_surface *child1 = wl_compositor_create_surface(c->compositor);
	struct wl_surface *parent2 = wl_compositor_create_surface(c->compositor);
	struct wl_surface *child2 = wl_compositor_create_surface(c->compositor);
	struct wl_subsurface *sub1 = wl_subcompositor_get_subsurface(
		c->subcompositor, child1, parent1);
	wl_subcompositor_get_subsurface(c->subcompositor, child2, parent2);
	wl_subsurface_place_below(sub1, child2);
}

/* Weston subsurface-test.c: test_subsurface_place_above_child. */
static void
bad_subsurface_place_above_child(struct conn *c)
{
	struct wl_surface *parent = wl_compositor_create_surface(c->compositor);
	struct wl_surface *child = wl_compositor_create_surface(c->compositor);
	struct wl_surface *grandchild
		= wl_compositor_create_surface(c->compositor);
	struct wl_subsurface *sub = wl_subcompositor_get_subsurface(
		c->subcompositor, child, parent);
	wl_subcompositor_get_subsurface(c->subcompositor, grandchild, child);
	wl_subsurface_place_above(sub, grandchild);
}

/* Weston subsurface-test.c: compound surface (parent + 3 children). */
#define NUM_SUBSURFACES 3

struct compound {
	struct wl_surface *parent;
	struct wl_surface *child[NUM_SUBSURFACES];
	struct wl_subsurface *sub[NUM_SUBSURFACES];
};

static void
populate_compound(struct conn *c, struct compound *com)
{
	com->parent = wl_compositor_create_surface(c->compositor);
	for (int i = 0; i < NUM_SUBSURFACES; i++) {
		com->child[i] = wl_compositor_create_surface(c->compositor);
		com->sub[i] = wl_subcompositor_get_subsurface(c->subcompositor,
			com->child[i], com->parent);
	}
}

/* Grandchild under child[0]; returns its wl_subsurface. */
static struct wl_subsurface *
add_grandchild(struct conn *c, struct compound *com,
	struct wl_surface **grandchild)
{
	*grandchild = wl_compositor_create_surface(c->compositor);
	return wl_subcompositor_get_subsurface(c->subcompositor, *grandchild,
		com->child[0]);
}

/* Weston subsurface-test.c: test_subsurface_place_above_grandparent. */
static void
bad_subsurface_place_above_grandparent(struct conn *c)
{
	struct compound com;
	struct wl_surface *grandchild;
	populate_compound(c, &com);
	wl_subsurface_place_above(add_grandchild(c, &com, &grandchild),
		com.parent);
}

/* Weston subsurface-test.c: test_subsurface_place_above_great_aunt. */
static void
bad_subsurface_place_above_great_aunt(struct conn *c)
{
	struct compound com;
	struct wl_surface *grandchild;
	populate_compound(c, &com);
	wl_subsurface_place_above(add_grandchild(c, &com, &grandchild),
		com.child[1]);
}

/* Weston subsurface-test.c: test_subsurface_place_below_grandparent. */
static void
bad_subsurface_place_below_grandparent(struct conn *c)
{
	struct compound com;
	struct wl_surface *grandchild;
	populate_compound(c, &com);
	wl_subsurface_place_below(add_grandchild(c, &com, &grandchild),
		com.parent);
}

/* Weston subsurface-test.c: test_subsurface_place_below_great_aunt. */
static void
bad_subsurface_place_below_great_aunt(struct conn *c)
{
	struct compound com;
	struct wl_surface *grandchild;
	populate_compound(c, &com);
	wl_subsurface_place_below(add_grandchild(c, &com, &grandchild),
		com.child[1]);
}

/* Weston subsurface-test.c: test_subsurface_place_below_child. */
static void
bad_subsurface_place_below_child(struct conn *c)
{
	struct compound com;
	struct wl_surface *grandchild;
	populate_compound(c, &com);
	add_grandchild(c, &com, &grandchild);
	wl_subsurface_place_below(com.sub[0], grandchild);
}

/* Weston subsurface-test.c: test_subsurface_place_below_stranger. */
static void
bad_subsurface_place_below_stranger(struct conn *c)
{
	struct compound com;
	struct wl_surface *stranger = wl_compositor_create_surface(c->compositor);
	populate_compound(c, &com);
	wl_subsurface_place_below(com.sub[0], stranger);
}

/* Weston subsurface-test.c: test_subsurface_place_above_foreign. */
static void
bad_subsurface_place_above_foreign(struct conn *c)
{
	struct compound com1, com2;
	populate_compound(c, &com1);
	populate_compound(c, &com2);
	wl_subsurface_place_above(com1.sub[0], com2.child[0]);
}

/* wayland.xml, wl_surface: destroying a surface before its role object
 * is defunct_role_object. */
static void
bad_surface_destroyed_before_role(struct conn *c)
{
	struct wl_surface *parent = wl_compositor_create_surface(c->compositor);
	struct wl_surface *child = wl_compositor_create_surface(c->compositor);
	wl_subcompositor_get_subsurface(c->subcompositor, child, parent);
	wl_surface_destroy(child);
}

/* xdg-shell.xml (xdg_surface): creating an xdg_surface from a wl_surface
 * that has a buffer attached or committed is a client error. The error is
 * xdg_wm_base.invalid_surface_state (the request is on xdg_wm_base; WLCS
 * XdgSurfaceStableTest expects the same). */
static void
bad_xdg_surface_from_committed_buffer(struct conn *c)
{
	struct wl_surface *surface = wl_compositor_create_surface(c->compositor);
	int fd = make_shm_fd(16 * 16 * 4);
	struct wl_shm_pool *pool = wl_shm_create_pool(c->shm, fd, 16 * 16 * 4);
	close(fd);
	struct wl_buffer *buffer = wl_shm_pool_create_buffer(pool, 0, 16, 16,
		16 * 4, WL_SHM_FORMAT_ARGB8888);
	wl_surface_attach(surface, buffer, 0, 0);
	wl_surface_commit(surface);
	wl_display_roundtrip(c->display);
	xdg_wm_base_get_xdg_surface(c->wm_base, surface);
}

static void
bad_xdg_surface_from_attached_buffer(struct conn *c)
{
	struct wl_surface *surface = wl_compositor_create_surface(c->compositor);
	int fd = make_shm_fd(16 * 16 * 4);
	struct wl_shm_pool *pool = wl_shm_create_pool(c->shm, fd, 16 * 16 * 4);
	close(fd);
	struct wl_buffer *buffer = wl_shm_pool_create_buffer(pool, 0, 16, 16,
		16 * 4, WL_SHM_FORMAT_ARGB8888);
	wl_surface_attach(surface, buffer, 0, 0);
	xdg_wm_base_get_xdg_surface(c->wm_base, surface);
}

/* xdg-shell.xml (xdg_surface): "any attempts by a client to attach or
 * manipulate a buffer prior to the first xdg_surface.configure call must
 * also be treated as errors" -- on the attach itself, without a commit
 * (WLCS XdgSurfaceStableTest.attaching_buffer_to_unconfigured_...). */
static void
bad_attach_before_first_configure(struct conn *c)
{
	struct wl_surface *surface = wl_compositor_create_surface(c->compositor);
	struct xdg_surface *xdg = xdg_wm_base_get_xdg_surface(c->wm_base,
		surface);
	xdg_surface_get_toplevel(xdg);
	int fd = make_shm_fd(16 * 16 * 4);
	struct wl_shm_pool *pool = wl_shm_create_pool(c->shm, fd, 16 * 16 * 4);
	close(fd);
	struct wl_buffer *buffer = wl_shm_pool_create_buffer(pool, 0, 16, 16,
		16 * 4, WL_SHM_FORMAT_ARGB8888);
	wl_surface_attach(surface, buffer, 0, 0);
}

struct negative_case {
	const char *name;
	bad_request request;
	const char *interface;
	uint32_t code;
};

static const struct negative_case kNegativeCases[] = {
	/* libwayland-server posts bind errors on the wl_registry object with
	 * the wl_display.invalid_object code (wayland-server.c,
	 * registry_bind). */
	{ "registry-bind-version-too-high", bad_bind_version,
		"wl_registry", WL_DISPLAY_ERROR_INVALID_OBJECT },
	{ "registry-bind-unknown-name", bad_bind_unknown_name,
		"wl_registry", WL_DISPLAY_ERROR_INVALID_OBJECT },
	{ "shm-pool-zero-size", bad_shm_pool_size,
		"wl_shm", WL_SHM_ERROR_INVALID_STRIDE },
	{ "shm-buffer-outside-pool", bad_shm_buffer_outside_pool,
		"wl_shm_pool", WL_SHM_ERROR_INVALID_STRIDE },
	{ "shm-buffer-unknown-format", bad_shm_buffer_format,
		"wl_shm_pool", WL_SHM_ERROR_INVALID_FORMAT },
	{ "shm-pool-shrink", bad_shm_pool_shrink,
		"wl_shm_pool", WL_SHM_ERROR_INVALID_STRIDE },
	{ "role-subsurface-then-xdg-surface", bad_role_subsurface_then_xdg,
		"xdg_wm_base", XDG_WM_BASE_ERROR_ROLE },
	{ "role-toplevel-then-subsurface", bad_role_toplevel_then_subsurface,
		"wl_subcompositor", WL_SUBCOMPOSITOR_ERROR_BAD_SURFACE },
	/* wayland.xml, wl_subcompositor.get_subsurface: a parent that is the
	 * surface itself or one of its descendants is bad_parent. Weston's
	 * subsurface-test.c still expects bad_surface (written before
	 * bad_parent existed); the spec wins. */
	{ "subsurface-own-parent", bad_subsurface_own_parent,
		"wl_subcompositor", WL_SUBCOMPOSITOR_ERROR_BAD_PARENT },
	{ "subsurface-identical-link", bad_subsurface_identical_link,
		"wl_subcompositor", WL_SUBCOMPOSITOR_ERROR_BAD_SURFACE },
	{ "subsurface-change-link", bad_subsurface_change_link,
		"wl_subcompositor", WL_SUBCOMPOSITOR_ERROR_BAD_SURFACE },
	{ "subsurface-loop", bad_subsurface_loop,
		"wl_subcompositor", WL_SUBCOMPOSITOR_ERROR_BAD_PARENT },
	{ "subsurface-place-above-stranger", bad_subsurface_place_above_stranger,
		"wl_subsurface", WL_SUBSURFACE_ERROR_BAD_SURFACE },
	{ "subsurface-place-below-foreign", bad_subsurface_place_below_foreign,
		"wl_subsurface", WL_SUBSURFACE_ERROR_BAD_SURFACE },
	{ "subsurface-place-above-child", bad_subsurface_place_above_child,
		"wl_subsurface", WL_SUBSURFACE_ERROR_BAD_SURFACE },
	{ "subsurface-place-above-grandparent",
		bad_subsurface_place_above_grandparent,
		"wl_subsurface", WL_SUBSURFACE_ERROR_BAD_SURFACE },
	{ "subsurface-place-above-great-aunt",
		bad_subsurface_place_above_great_aunt,
		"wl_subsurface", WL_SUBSURFACE_ERROR_BAD_SURFACE },
	{ "subsurface-place-below-grandparent",
		bad_subsurface_place_below_grandparent,
		"wl_subsurface", WL_SUBSURFACE_ERROR_BAD_SURFACE },
	{ "subsurface-place-below-great-aunt",
		bad_subsurface_place_below_great_aunt,
		"wl_subsurface", WL_SUBSURFACE_ERROR_BAD_SURFACE },
	{ "subsurface-place-below-child", bad_subsurface_place_below_child,
		"wl_subsurface", WL_SUBSURFACE_ERROR_BAD_SURFACE },
	{ "subsurface-place-below-stranger", bad_subsurface_place_below_stranger,
		"wl_subsurface", WL_SUBSURFACE_ERROR_BAD_SURFACE },
	{ "subsurface-place-above-foreign", bad_subsurface_place_above_foreign,
		"wl_subsurface", WL_SUBSURFACE_ERROR_BAD_SURFACE },
	/* The error is on the wl_surface the client just destroyed, so the
	 * client only sees the code. */
	{ "xdg-surface-from-committed-buffer",
		bad_xdg_surface_from_committed_buffer,
		"xdg_wm_base", XDG_WM_BASE_ERROR_INVALID_SURFACE_STATE },
	{ "xdg-surface-from-attached-buffer",
		bad_xdg_surface_from_attached_buffer,
		"xdg_wm_base", XDG_WM_BASE_ERROR_INVALID_SURFACE_STATE },
	{ "xdg-attach-before-first-configure",
		bad_attach_before_first_configure,
		"xdg_surface", XDG_SURFACE_ERROR_UNCONFIGURED_BUFFER },
	{ "surface-destroyed-before-role-object",
		bad_surface_destroyed_before_role,
		kDestroyedObject, WL_SURFACE_ERROR_DEFUNCT_ROLE_OBJECT },
};

static void
test_negative(const char *socket)
{
	struct conn witness;
	if (!conn_open(&witness, socket)) {
		result(ST_BLOCKED, "negative-witness", "could not connect");
		return;
	}

	for (size_t i = 0; i < sizeof(kNegativeCases) / sizeof(*kNegativeCases);
		i++) {
		const struct negative_case *t = &kNegativeCases[i];
		struct conn c;
		if (!conn_open(&c, socket) || c.compositor == NULL
			|| c.subcompositor == NULL || c.shm == NULL
			|| c.wm_base == NULL) {
			result(ST_BLOCKED, t->name, "client setup failed");
			conn_close(&c);
			continue;
		}

		t->request(&c);
		char detail[256];
		bool ok = expect_protocol_error(&c, t->interface, t->code, detail,
			sizeof(detail));
		conn_close(&c);

		/* The offender alone must be disconnected. */
		if (ok && wl_display_roundtrip(witness.display) < 0) {
			ok = false;
			strncat(detail, "; the witness client was disconnected too",
				sizeof(detail) - strlen(detail) - 1);
		}
		result(ok ? ST_PASS : ST_FAIL, t->name, "%s", detail);
	}

	/* The server must still accept and serve new clients. */
	struct conn after;
	bool alive = conn_open(&after, socket) && after.compositor != NULL;
	result(alive ? ST_PASS : ST_FAIL, "server-alive-after-errors",
		alive ? "new client served" : "new client failed");
	conn_close(&after);

	bool witness_ok = wl_display_roundtrip(witness.display) >= 0;
	result(witness_ok ? ST_PASS : ST_FAIL, "witness-connected-throughout",
		witness_ok ? "witness roundtrip ok" : "witness roundtrip failed");
	conn_close(&witness);
}

/* wl_output v4 sends name and description on bind (wayland.xml). A NULL
 * string cannot be marshalled: the server logged "error marshalling
 * arguments for name" and disconnected the client, which is how Chromium
 * (--ozone-platform=wayland) failed to start. */
struct output_info {
	char name[64];
	char description[128];
	bool done;
	int32_t width;              /* current mode */
	int32_t height;
	int32_t refresh;
	int32_t scale;
	int modes;                  /* wl_output.mode events received */
};

static void output_geometry(void *data, struct wl_output *o, int32_t x,
	int32_t y, int32_t pw, int32_t ph, int32_t subpixel, const char *make,
	const char *model, int32_t transform)
{
	(void)data; (void)o; (void)x; (void)y; (void)pw; (void)ph;
	(void)subpixel; (void)make; (void)model; (void)transform;
}

static void output_mode(void *data, struct wl_output *o, uint32_t flags,
	int32_t w, int32_t h, int32_t refresh)
{
	(void)o;
	struct output_info *info = data;
	if ((flags & WL_OUTPUT_MODE_CURRENT) == 0)
		return;
	info->width = w;
	info->height = h;
	info->refresh = refresh;
	info->modes++;
}

static void output_done(void *data, struct wl_output *o)
{
	(void)o;
	((struct output_info *)data)->done = true;
}

static void output_scale(void *data, struct wl_output *o, int32_t factor)
{
	(void)o;
	((struct output_info *)data)->scale = factor;
}

static void output_name(void *data, struct wl_output *o, const char *name)
{
	(void)o;
	snprintf(((struct output_info *)data)->name,
		sizeof(((struct output_info *)data)->name), "%s", name);
}

static void output_description(void *data, struct wl_output *o,
	const char *description)
{
	(void)o;
	snprintf(((struct output_info *)data)->description,
		sizeof(((struct output_info *)data)->description), "%s", description);
}

static const struct wl_output_listener sOutputListener = {
	.geometry = output_geometry,
	.mode = output_mode,
	.done = output_done,
	.scale = output_scale,
	.name = output_name,
	.description = output_description,
};

static void
test_output_v4(const char *socket)
{
	struct conn c;
	int count = 0;
	const struct global *g = conn_open(&c, socket)
		? find_global(&c, "wl_output", &count) : NULL;
	if (g == NULL || g->version < 4) {
		result(ST_BLOCKED, "output-v4-name", "no wl_output v4");
		conn_close(&c);
		return;
	}
	struct output_info info = {0};
	struct wl_output *output = wl_registry_bind(c.registry, g->name,
		&wl_output_interface, 4);
	wl_output_add_listener(output, &sOutputListener, &info);
	bool ok = settle(&c) && wl_display_get_error(c.display) == 0;
	result(ok && info.done && info.name[0] != '\0'
			&& info.description[0] != '\0' ? ST_PASS : ST_FAIL,
		"output-v4-name", "%s; name '%s', description '%s'",
		ok ? "connected" : "DISCONNECTED", info.name, info.description);
	conn_close(&c);
}

/* app_server reports the screen's mode with KOAN_CMD_OUTPUT_MODE
 * (WaylandWindowBridge::ScreenChanged); bound clients must get it as the
 * current wl_output mode and scale. */
static void
test_output_mode(const char *socket)
{
	struct conn c;
	int count = 0;
	const struct global *g = conn_open(&c, socket)
		? find_global(&c, "wl_output", &count) : NULL;
	if (g == NULL) {
		result(ST_BLOCKED, "output-mode", "no wl_output");
		conn_close(&c);
		return;
	}
	struct output_info info = {0};
	struct wl_output *output = wl_registry_bind(c.registry, g->name,
		&wl_output_interface, g->version < 2 ? g->version : 2);
	wl_output_add_listener(output, &sOutputListener, &info);
	bool ok = settle(&c);
	int initialModes = info.modes;

	union koan_cmd_payload mode;
	memset(&mode, 0, sizeof(mode));
	mode.output_mode.width = 5120;
	mode.output_mode.height = 2880;
	mode.output_mode.refresh_mhz = 59951;
	mode.output_mode.scale = 2.0f;
	ok = ok && koan_queue_push(koan_adapter_queue(), KOAN_CMD_OUTPUT_MODE,
		&mode, NULL);

	/* The queue wakes the Wayland thread through its own fd, so the
	 * events may trail a roundtrip. */
	for (int i = 0; ok && i < 50 && info.modes == initialModes; i++) {
		ok = settle(&c);
		usleep(2000);
	}
	ok = ok && wl_display_get_error(c.display) == 0;
	result(ok && info.width == 5120 && info.height == 2880
			&& info.refresh == 59951 && info.scale == 2 ? ST_PASS : ST_FAIL,
		"output-mode", "%s; current mode %dx%d@%d mHz, scale %d",
		ok ? "connected" : "DISCONNECTED", info.width, info.height,
		info.refresh, info.scale);
	conn_close(&c);
}

static int
run_core(void)
{
	struct server s;
	if (!server_start(&s)) {
		result(ST_BLOCKED, "server-start", "koan server failed to start");
		server_stop(&s);
		return summary();
	}
	result(ST_PASS, "server-start", "socket %s", s.socket);

	test_registry(s.socket);
	test_negative(s.socket);
	test_output_v4(s.socket);
	test_output_mode(s.socket);

	server_stop(&s);
	result(ST_PASS, "server-shutdown", "terminated and destroyed");
	return summary();
}

/* ---- lifecycle --------------------------------------------------------- */

static int
run_lifecycle(void)
{
	for (int round = 1; round <= 2; round++) {
		char name[64];
		snprintf(name, sizeof(name), "lifecycle-round-%d", round);

		struct server s;
		if (!server_start(&s)) {
			result(ST_FAIL, name, "server failed to start");
			server_stop(&s);
			return summary();
		}
		struct conn c;
		bool ok = conn_open(&c, s.socket) && c.compositor != NULL;
		struct wl_callback *sync = ok ? wl_display_sync(c.display) : NULL;
		ok = ok && wl_display_roundtrip(c.display) >= 0;
		if (sync != NULL)
			wl_callback_destroy(sync);
		conn_close(&c);
		server_stop(&s);
		result(ok ? ST_PASS : ST_FAIL, name,
			ok ? "start, connect, sync, disconnect, stop"
				: "client could not use the server");
	}
	return summary();
}

/* ---- bad-buffer -------------------------------------------------------- */

struct toplevel {
	struct wl_surface *surface;
	struct xdg_surface *xdg_surface;
	struct xdg_toplevel *xdg_toplevel;
	uint32_t configure_serial;
	bool configured;
	int configures;             /* xdg_surface.configure events seen */
	int outputs_entered;
	int32_t width, height;      /* last xdg_toplevel.configure */
	bool maximized, fullscreen, activated;
};

static void
toplevel_configure(void *data, struct xdg_toplevel *xdg_toplevel,
	int32_t width, int32_t height, struct wl_array *states)
{
	(void)xdg_toplevel;
	struct toplevel *t = data;
	t->width = width;
	t->height = height;
	t->maximized = t->fullscreen = t->activated = false;
	uint32_t *state;
	wl_array_for_each(state, states) {
		if (*state == XDG_TOPLEVEL_STATE_MAXIMIZED)
			t->maximized = true;
		else if (*state == XDG_TOPLEVEL_STATE_FULLSCREEN)
			t->fullscreen = true;
		else if (*state == XDG_TOPLEVEL_STATE_ACTIVATED)
			t->activated = true;
	}
}

static void
toplevel_close(void *data, struct xdg_toplevel *xdg_toplevel)
{
	(void)data;
	(void)xdg_toplevel;
}

static void
toplevel_configure_bounds(void *data, struct xdg_toplevel *xdg_toplevel,
	int32_t width, int32_t height)
{
	(void)data;
	(void)xdg_toplevel;
	(void)width;
	(void)height;
}

static void
toplevel_wm_capabilities(void *data, struct xdg_toplevel *xdg_toplevel,
	struct wl_array *capabilities)
{
	(void)data;
	(void)xdg_toplevel;
	(void)capabilities;
}

static const struct xdg_toplevel_listener sToplevelListener = {
	.configure = toplevel_configure,
	.close = toplevel_close,
	.configure_bounds = toplevel_configure_bounds,
	.wm_capabilities = toplevel_wm_capabilities,
};

static void
surface_enter(void *data, struct wl_surface *surface,
	struct wl_output *output)
{
	(void)surface;
	(void)output;
	((struct toplevel *)data)->outputs_entered++;
}

static void
surface_leave(void *data, struct wl_surface *surface,
	struct wl_output *output)
{
	(void)surface;
	(void)output;
	((struct toplevel *)data)->outputs_entered--;
}

/* wl_compositor is bound at version 4: only enter/leave can arrive. */
static const struct wl_surface_listener sSurfaceListener = {
	.enter = surface_enter,
	.leave = surface_leave,
};

static void
xdg_surface_configure(void *data, struct xdg_surface *xdg_surface,
	uint32_t serial)
{
	(void)xdg_surface;
	struct toplevel *t = data;
	t->configure_serial = serial;
	t->configured = true;
	t->configures++;
}

static const struct xdg_surface_listener sXdgSurfaceListener = {
	.configure = xdg_surface_configure,
};

static struct wl_buffer *
make_buffer(struct conn *c, int width, int height, uint32_t pixel, int *fd_out)
{
	size_t size = (size_t)width * height * 4;
	int fd = make_shm_fd(size);
	if (fd < 0)
		return NULL;
	uint32_t *pixels = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED,
		fd, 0);
	if (pixels == MAP_FAILED) {
		close(fd);
		return NULL;
	}
	for (size_t i = 0; i < size / 4; i++)
		pixels[i] = pixel;
	munmap(pixels, size);

	struct wl_shm_pool *pool = wl_shm_create_pool(c->shm, fd, (int32_t)size);
	struct wl_buffer *buffer = wl_shm_pool_create_buffer(pool, 0, width,
		height, width * 4, WL_SHM_FORMAT_ARGB8888);
	wl_shm_pool_destroy(pool);
	if (fd_out != NULL)
		*fd_out = fd;
	else
		close(fd);
	return buffer;
}

static int
run_bad_buffer(void)
{
	struct server s;
	if (!server_start(&s)) {
		result(ST_BLOCKED, "bad-buffer-truncated-shm", "server start");
		server_stop(&s);
		return summary();
	}

	struct conn witness;
	struct conn c;
	if (!conn_open(&witness, s.socket) || !conn_open(&c, s.socket)
		|| c.wm_base == NULL) {
		result(ST_BLOCKED, "bad-buffer-truncated-shm", "client setup");
		server_stop(&s);
		return summary();
	}

	/* Map a toplevel the way the xdg test does, with app_server's
	 * WINDOW_READY reply, so commits reach the adapter's pixel copy. */
	struct toplevel t = {0};
	t.surface = wl_compositor_create_surface(c.compositor);
	t.xdg_surface = xdg_wm_base_get_xdg_surface(c.wm_base, t.surface);
	xdg_surface_add_listener(t.xdg_surface, &sXdgSurfaceListener, &t);
	t.xdg_toplevel = xdg_surface_get_toplevel(t.xdg_surface);
	wl_surface_commit(t.surface);
	bool ok = settle(&c) && t.configured;
	if (!ok) {
		result(ST_BLOCKED, "bad-buffer-truncated-shm", "no configure");
		server_stop(&s);
		return summary();
	}
	xdg_surface_ack_configure(t.xdg_surface, t.configure_serial);
	struct wl_buffer *good = make_buffer(&c, 64, 48, 0xff336699u, NULL);
	wl_surface_attach(t.surface, good, 0, 0);
	wl_surface_damage(t.surface, 0, 0, 64, 48);
	wl_surface_commit(t.surface);
	ok = settle(&c) && atomic_load(&sCreatedToplevel) != NULL;
	if (ok) {
		union koan_cmd_payload ready = {0};
		ready.window_ready.toplevel = atomic_load(&sCreatedToplevel);
		ready.window_ready.window = &sNativeWindowToken;
		ok = koan_queue_push(koan_adapter_queue(), KOAN_CMD_WINDOW_READY,
			&ready, NULL) != NULL && settle(&c)
			&& atomic_load(&sSetBufferCount) >= 1;
	}
	if (!ok) {
		result(ST_BLOCKED, "bad-buffer-truncated-shm",
			"toplevel did not map (set_buffer count %d)",
			atomic_load(&sSetBufferCount));
		server_stop(&s);
		return summary();
	}

	/* Weston bad-buffer-test.c: a 200x200 buffer whose file shrinks to
	 * 12 bytes after the pool is created. */
	int fd = -1;
	struct wl_buffer *bad = make_buffer(&c, 200, 200, 0xffff0000u, &fd);
	if (bad == NULL || ftruncate(fd, 12) != 0) {
		result(ST_BLOCKED, "bad-buffer-truncated-shm", "buffer setup");
		server_stop(&s);
		return summary();
	}
	close(fd);
	int before = atomic_load(&sSetBufferCount);
	wl_surface_attach(t.surface, bad, 0, 0);
	wl_surface_damage(t.surface, 0, 0, 200, 200);
	wl_surface_commit(t.surface);

	bool offender_alive = settle(&c);
	char error[160] = "no protocol error (wlroots zero-fills the mapping)";
	if (!offender_alive) {
		const struct wl_interface *iface = NULL;
		uint32_t id = 0;
		uint32_t code = wl_display_get_protocol_error(c.display, &iface,
			&id);
		snprintf(error, sizeof(error), "client disconnected: %s#%u",
			iface != NULL ? iface->name : "(none)", code);
	}
	bool witness_ok = wl_display_roundtrip(witness.display) >= 0;
	struct conn after;
	bool server_ok = conn_open(&after, s.socket) && after.compositor != NULL;
	conn_close(&after);

	result(witness_ok && server_ok ? ST_PASS : ST_FAIL,
		"bad-buffer-truncated-shm",
		"server %s, witness %s; offender: %s; set_buffer after: %d",
		server_ok ? "alive" : "DEAD", witness_ok ? "connected" : "LOST",
		error, atomic_load(&sSetBufferCount) - before);

	conn_close(&c);
	conn_close(&witness);
	server_stop(&s);
	return summary();
}

/* ---- subsurface: valid sequences (Weston subsurface-test.c) ----------- */

/* A valid request sequence must leave the client connected. */
static void
expect_no_error(struct conn *c, const char *name)
{
	bool ok = settle(c);
	if (ok) {
		result(ST_PASS, name, "no protocol error");
		return;
	}
	const struct wl_interface *iface = NULL;
	uint32_t id = 0;
	uint32_t code = wl_display_get_protocol_error(c->display, &iface, &id);
	result(ST_FAIL, name, "unexpected error %s#%u on object %u",
		iface != NULL ? iface->name : "(none)", code, id);
}

typedef void (*valid_sequence)(struct conn *c);

/* Weston: test_subsurface_basic_protocol. */
static void
seq_basic(struct conn *c)
{
	struct compound com1, com2;
	populate_compound(c, &com1);
	populate_compound(c, &com2);
}

/* Weston: test_subsurface_position_protocol. */
static void
seq_position(struct conn *c)
{
	struct compound com;
	populate_compound(c, &com);
	for (int i = 0; i < NUM_SUBSURFACES; i++)
		wl_subsurface_set_position(com.sub[i], (i + 2) * 20, (i + 2) * 10);
}

/* Weston: test_subsurface_placement_protocol. */
static void
seq_placement(struct conn *c)
{
	struct compound com;
	populate_compound(c, &com);
	wl_subsurface_place_above(com.sub[0], com.child[1]);
	wl_subsurface_place_above(com.sub[1], com.parent);
	wl_subsurface_place_below(com.sub[2], com.child[0]);
	wl_subsurface_place_below(com.sub[1], com.parent);
}

/* Weston: test_subsurface_nesting (the parent is a sub-surface). */
static void
seq_nesting(struct conn *c)
{
	struct compound com;
	struct wl_surface *stranger = wl_compositor_create_surface(c->compositor);
	populate_compound(c, &com);
	wl_subcompositor_get_subsurface(c->subcompositor, stranger,
		com.child[0]);
}

/* Weston: test_subsurface_nesting_parent (the surface is a parent). */
static void
seq_nesting_parent(struct conn *c)
{
	struct compound com;
	struct wl_surface *stranger = wl_compositor_create_surface(c->compositor);
	populate_compound(c, &com);
	wl_subcompositor_get_subsurface(c->subcompositor, com.parent, stranger);
}

/* Weston: test_subsurface_place_above_nested_parent. */
static void
seq_place_above_nested_parent(struct conn *c)
{
	struct compound com;
	struct wl_surface *grandchild;
	populate_compound(c, &com);
	wl_subsurface_place_above(add_grandchild(c, &com, &grandchild),
		com.child[0]);
}

/* Weston: test_subsurface_place_below_nested_parent. */
static void
seq_place_below_nested_parent(struct conn *c)
{
	struct compound com;
	struct wl_surface *grandchild;
	populate_compound(c, &com);
	wl_subsurface_place_below(add_grandchild(c, &com, &grandchild),
		com.child[0]);
}

/*
 * Weston: test_subsurface_destroy_protocol, in an order the current
 * protocol allows. wayland.xml (wl_surface) now requires a role object to
 * be destroyed before its wl_surface (defunct_role_object); Weston's
 * version destroys child surfaces first, which is tested as an error in
 * "surface-destroyed-before-role-object" instead.
 */
static void
seq_destroy(struct conn *c)
{
	struct compound com;
	populate_compound(c, &com);
	wl_subsurface_destroy(com.sub[0]);   /* detach child from parent */
	wl_subsurface_destroy(com.sub[1]);   /* destroy: child, parent */
	wl_surface_destroy(com.child[1]);
	wl_surface_destroy(com.parent);      /* parent while sub[2] lives */
	wl_subsurface_destroy(com.sub[2]);   /* destroy: parent, child */
	wl_surface_destroy(com.child[2]);
	wl_surface_destroy(com.child[0]);    /* destroy: sub, child */
}

/*
 * Weston: test_subsurface_destroy_permutations. Every ordered choice of
 * NSTEPS objects (of 11 surfaces and their 10 wl_subsurfaces) is destroyed
 * first in this tree, then the rest:
 *                            0
 *                           / \
 *                          1   2 - 10
 *                         / \  |\
 *                        3   5 9 6
 *                       /       / \
 *                      4       7   8
 */
#define PERMU_TREE 11
#define PERMU_STEPS 3

static void
create_tree(struct conn *c, struct wl_surface **surfs,
	struct wl_subsurface **subs)
{
	static const int kParent[PERMU_TREE]
		= { -1, 0, 0, 1, 3, 1, 2, 6, 6, 2, 2 };
	for (int i = 0; i < PERMU_TREE; i++)
		surfs[i] = wl_compositor_create_surface(c->compositor);
	for (int i = PERMU_TREE - 1; i > 0; i--) {
		subs[i] = wl_subcompositor_get_subsurface(c->subcompositor,
			surfs[i], surfs[kParent[i]]);
	}
	subs[0] = NULL;
}

static void
destroy_tree(struct wl_surface **surfs, struct wl_subsurface **subs)
{
	for (int i = PERMU_TREE; i-- > 0;) {
		if (subs[i] != NULL)
			wl_subsurface_destroy(subs[i]);
		if (surfs[i] != NULL)
			wl_surface_destroy(surfs[i]);
	}
}

static void
destroy_object(struct wl_surface **surfs, struct wl_subsurface **subs,
	int index)
{
	int h = (index + 1) / 2;
	if (index & 1) {
		if (subs[h] != NULL)
			wl_subsurface_destroy(subs[h]);
		subs[h] = NULL;
	} else {
		/* The role object goes first (defunct_role_object). */
		if (subs[h] != NULL)
			wl_subsurface_destroy(subs[h]);
		subs[h] = NULL;
		wl_surface_destroy(surfs[h]);
		surfs[h] = NULL;
	}
}

static void
test_destroy_permutations(const char *socket)
{
	struct conn c;
	if (!conn_open(&c, socket) || c.subcompositor == NULL) {
		result(ST_BLOCKED, "subsurface-destroy-permutations", "setup");
		conn_close(&c);
		return;
	}

	const int set_size = PERMU_TREE * 2 - 1;
	int tried = 0;
	bool ok = true;
	for (int a = 0; ok && a < set_size; a++) {
		for (int b = 0; ok && b < set_size; b++) {
			for (int d = 0; ok && d < set_size; d++) {
				if (a == b || a == d || b == d)
					continue;
				/* Destroying surface h takes sub h (index 2h-1)
				 * down first; a later step on sub h is then a
				 * no-op (destroy_object checks for NULL). */
				struct wl_surface *surfs[PERMU_TREE];
				struct wl_subsurface *subs[PERMU_TREE];
				create_tree(&c, surfs, subs);
				destroy_object(surfs, subs, a);
				destroy_object(surfs, subs, b);
				destroy_object(surfs, subs, d);
				ok = wl_display_roundtrip(c.display) >= 0;
				destroy_tree(surfs, subs);
				tried++;
			}
		}
	}
	ok = ok && wl_display_roundtrip(c.display) >= 0;
	result(ok ? ST_PASS : ST_FAIL, "subsurface-destroy-permutations",
		"%d permutations%s", tried, ok ? "" : ", client disconnected");
	conn_close(&c);
}

static int
run_subsurface(void)
{
	static const struct {
		const char *name;
		valid_sequence run;
	} kSequences[] = {
		{ "subsurface-basic-protocol", seq_basic },
		{ "subsurface-position-protocol", seq_position },
		{ "subsurface-placement-protocol", seq_placement },
		{ "subsurface-nesting", seq_nesting },
		{ "subsurface-nesting-parent", seq_nesting_parent },
		{ "subsurface-place-above-nested-parent",
			seq_place_above_nested_parent },
		{ "subsurface-place-below-nested-parent",
			seq_place_below_nested_parent },
		{ "subsurface-destroy-protocol", seq_destroy },
	};

	struct server s;
	if (!server_start(&s)) {
		result(ST_BLOCKED, "subsurface", "server start");
		server_stop(&s);
		return summary();
	}
	for (size_t i = 0; i < sizeof(kSequences) / sizeof(*kSequences); i++) {
		struct conn c;
		if (!conn_open(&c, s.socket) || c.subcompositor == NULL) {
			result(ST_BLOCKED, kSequences[i].name, "client setup");
			conn_close(&c);
			continue;
		}
		kSequences[i].run(&c);
		expect_no_error(&c, kSequences[i].name);
		conn_close(&c);
	}
	test_destroy_permutations(s.socket);
	server_stop(&s);
	return summary();
}

/* ---- compose: what reaches app_server ---------------------------------- */

#define COLOR_ROOT 0xffcc0000u
#define COLOR_CHILD 0xff00cc00u
#define COLOR_GRAND 0xff0000ccu
#define COLOR_GRAND_2 0xffcccc00u
#define COLOR_GRAND_3 0xff00cccc

struct frame_wait {
	int done;
};

static void
frame_done(void *data, struct wl_callback *callback, uint32_t time)
{
	(void)time;
	((struct frame_wait *)data)->done++;
	wl_callback_destroy(callback);
}

static const struct wl_callback_listener sFrameListener = {
	.done = frame_done,
};

/* Maps a 64x48 toplevel and replies WINDOW_READY like app_server. */
static bool
map_toplevel(struct conn *c, struct toplevel *t, uint32_t color)
{
	memset(t, 0, sizeof(*t));
	t->surface = wl_compositor_create_surface(c->compositor);
	wl_surface_add_listener(t->surface, &sSurfaceListener, t);
	t->xdg_surface = xdg_wm_base_get_xdg_surface(c->wm_base, t->surface);
	xdg_surface_add_listener(t->xdg_surface, &sXdgSurfaceListener, t);
	t->xdg_toplevel = xdg_surface_get_toplevel(t->xdg_surface);
	xdg_toplevel_add_listener(t->xdg_toplevel, &sToplevelListener, t);
	wl_surface_commit(t->surface);
	if (!settle(c) || !t->configured)
		return false;
	xdg_surface_ack_configure(t->xdg_surface, t->configure_serial);
	struct wl_buffer *buffer = make_buffer(c, 64, 48, color, NULL);
	wl_surface_attach(t->surface, buffer, 0, 0);
	wl_surface_damage(t->surface, 0, 0, 64, 48);
	wl_surface_commit(t->surface);
	if (!settle(c) || atomic_load(&sCreatedToplevel) == NULL)
		return false;
	union koan_cmd_payload ready = {0};
	ready.window_ready.toplevel = atomic_load(&sCreatedToplevel);
	ready.window_ready.window = &sNativeWindowToken;
	return koan_queue_push(koan_adapter_queue(), KOAN_CMD_WINDOW_READY,
		&ready, NULL) != NULL && settle(c)
		&& atomic_load(&sSetBufferCount) >= 1;
}

static void
attach_color(struct conn *c, struct wl_surface *surface, int w, int h,
	uint32_t color)
{
	struct wl_buffer *buffer = make_buffer(c, w, h, color, NULL);
	wl_surface_attach(surface, buffer, 0, 0);
	wl_surface_damage(surface, 0, 0, w, h);
}

/* Dispatches until *counter reaches target or about a second passes. */
static bool
wait_for_count(struct conn *c, const int *counter, int target)
{
	for (int i = 0; i < 500 && *counter < target; i++) {
		if (wl_display_roundtrip(c->display) < 0)
			return false;
		if (*counter < target)
			usleep(2000);
	}
	return *counter >= target;
}

static void
test_frame_loop(struct conn *c, struct toplevel *t)
{
	const int kFrames = 60;
	struct frame_wait wait = {0};
	int before = atomic_load(&sSetBufferCount);
	bool ok = true;
	for (int i = 0; ok && i < kFrames; i++) {
		attach_color(c, t->surface, 64, 48,
			(i & 1) ? COLOR_ROOT : COLOR_CHILD);
		struct wl_callback *cb = wl_surface_frame(t->surface);
		wl_callback_add_listener(cb, &sFrameListener, &wait);
		wl_surface_commit(t->surface);
		/* Like a throttled client: wait for this frame's callback. */
		ok = wait_for_count(c, &wait.done, i + 1);
	}
	int delivered = atomic_load(&sSetBufferCount) - before;
	uint32_t last = ((kFrames - 1) & 1) ? COLOR_ROOT : COLOR_CHILD;
	ok = ok && wait.done == kFrames && delivered == kFrames
		&& frame_pixel(0, 0) == last;
	result(ok ? ST_PASS : ST_FAIL, "frame-callback-loop",
		"%d/%d callbacks, %d frames delivered, last pixel 0x%08x",
		wait.done, kFrames, delivered, frame_pixel(0, 0));
}

/*
 * A frame callback must not be answered as part of the commit that
 * requested it: a client that requests a frame on every callback (Firefox
 * does) would then loop without ever sleeping. It has to come later, at
 * the output's pace. Same requirement as WLCS
 * FrameSubmission.when_client_endlessly_requests_frame_then_callbacks_are_throttled.
 */
static void
test_frame_throttle(struct conn *c, struct toplevel *t)
{
	const int kRounds = 20;
	int immediate = 0;
	struct frame_wait wait = {0};
	bool ok = true;
	for (int i = 0; ok && i < kRounds; i++) {
		struct wl_callback *cb = wl_surface_frame(t->surface);
		wl_callback_add_listener(cb, &sFrameListener, &wait);
		wl_surface_commit(t->surface);
		/* One roundtrip: the server has processed the commit. */
		ok = wl_display_roundtrip(c->display) >= 0;
		if (wait.done > i)
			immediate++;
		ok = ok && wait_for_count(c, &wait.done, i + 1);
	}
	result(ok && immediate == 0 ? ST_PASS : ST_FAIL,
		"frame-callback-throttled",
		"%d of %d callbacks were answered within the commit's own "
		"roundtrip; %d arrived in total", immediate, kRounds, wait.done);
}

static void
test_surface_enter(struct conn *c, struct toplevel *t)
{
	bool ok = settle(c);
	result(ok && t->outputs_entered == 1 ? ST_PASS : ST_FAIL,
		"surface-enters-output",
		"mapped toplevel is on %d outputs (expected 1 wl_surface.enter)",
		t->outputs_entered);
}

static void
test_nested_sync(struct conn *c, struct toplevel *t)
{
	/* root (64x48) <- child C (sync, 32x48 at 0,0)
	 *              <- grandchild G (desync request, 16x16 at 0,0 of C).
	 * G is effectively synchronized because C is: its commits must not
	 * show until the root's state is applied (wayland.xml,
	 * wl_subsurface). */
	struct wl_surface *child = wl_compositor_create_surface(c->compositor);
	struct wl_surface *grand = wl_compositor_create_surface(c->compositor);
	struct wl_subsurface *child_sub = wl_subcompositor_get_subsurface(
		c->subcompositor, child, t->surface);
	struct wl_subsurface *grand_sub = wl_subcompositor_get_subsurface(
		c->subcompositor, grand, child);
	wl_subsurface_set_desync(grand_sub);
	attach_color(c, grand, 16, 16, COLOR_GRAND);
	wl_surface_commit(grand);
	attach_color(c, child, 32, 48, COLOR_CHILD);
	wl_surface_commit(child);
	attach_color(c, t->surface, 64, 48, COLOR_ROOT);
	wl_surface_commit(t->surface);
	bool ok = settle(c);
	uint32_t g = frame_pixel(0, 0), ch = frame_pixel(20, 20),
		r = frame_pixel(40, 20);
	result(ok && g == COLOR_GRAND && ch == COLOR_CHILD && r == COLOR_ROOT
			? ST_PASS : ST_FAIL, "nested-tree-composed",
		"grandchild 0x%08x child 0x%08x root 0x%08x", g, ch, r);

	/* Effectively synchronized: G's own commit must not show. */
	attach_color(c, grand, 16, 16, COLOR_GRAND_2);
	wl_surface_commit(grand);
	ok = settle(c);
	g = frame_pixel(0, 0);
	result(ok && g == COLOR_GRAND ? ST_PASS : ST_FAIL,
		"nested-sync-grandchild-waits",
		"after the grandchild's own commit the window shows 0x%08x "
		"(expected the old 0x%08x)", g, COLOR_GRAND);

	/* ... and shows once the chain up to the root is committed. */
	wl_surface_commit(child);
	wl_surface_commit(t->surface);
	ok = settle(c);
	g = frame_pixel(0, 0);
	result(ok && g == COLOR_GRAND_2 ? ST_PASS : ST_FAIL,
		"nested-sync-applied-with-root",
		"after child+root commits the window shows 0x%08x (expected "
		"0x%08x)", g, COLOR_GRAND_2);

	/* With C desynchronized too, G is effectively desync: its commit
	 * shows on its own. */
	wl_subsurface_set_desync(child_sub);
	wl_surface_commit(child);
	settle(c);
	attach_color(c, grand, 16, 16, COLOR_GRAND_3);
	wl_surface_commit(grand);
	ok = settle(c);
	g = frame_pixel(0, 0);
	result(ok && g == COLOR_GRAND_3 ? ST_PASS : ST_FAIL,
		"nested-desync-grandchild-immediate",
		"after the grandchild's own commit the window shows 0x%08x "
		"(expected 0x%08x)", g, COLOR_GRAND_3);

	/* Position is parent state: set_position on a sync child applies
	 * with the parent's commit. */
	wl_subsurface_set_sync(child_sub);
	wl_subsurface_set_position(child_sub, 32, 0);
	wl_surface_commit(child);
	settle(c);
	uint32_t before_root = frame_pixel(40, 20);
	wl_surface_commit(t->surface);
	ok = settle(c);
	uint32_t after_root = frame_pixel(40, 20);
	result(ok && before_root == COLOR_ROOT && after_root == COLOR_CHILD
			? ST_PASS : ST_FAIL, "subsurface-position-on-parent-commit",
		"pixel (40,20): 0x%08x before the root commit, 0x%08x after "
		"(expected root then child colour)", before_root, after_root);

	/* Parent destroyed while the grandchild has pending state. */
	attach_color(c, grand, 16, 16, COLOR_GRAND);
	wl_subsurface_set_sync(grand_sub);
	wl_surface_commit(grand);
	wl_subsurface_destroy(child_sub);
	wl_surface_destroy(child);
	wl_surface_commit(t->surface);
	ok = settle(c);
	result(ok ? ST_PASS : ST_FAIL, "subsurface-parent-destroyed-pending",
		ok ? "client and server fine" : "client disconnected");
	wl_subsurface_destroy(grand_sub);
	wl_surface_destroy(grand);
	settle(c);
}

/* Sets the output's mode and scale as app_server does and waits until the
 * Wayland thread has applied it (the queue has its own wake-up fd). */
static bool
set_output_scale(struct conn *c, float scale)
{
	union koan_cmd_payload mode;
	memset(&mode, 0, sizeof(mode));
	mode.output_mode.width = 2560;
	mode.output_mode.height = 1440;
	mode.output_mode.refresh_mhz = 60000;
	mode.output_mode.scale = scale;
	if (koan_queue_push(koan_adapter_queue(), KOAN_CMD_OUTPUT_MODE, &mode,
			NULL) == NULL)
		return false;
	int32_t expected = scale > (int32_t)scale ? (int32_t)scale + 1
		: (int32_t)scale;
	for (int i = 0; i < 100; i++) {
		if (!settle(c))
			return false;
		/* The output is only touched on the Wayland thread; the value
		 * read here may lag but settles. */
		if (koan_server_output_scale() == expected)
			return true;
		usleep(2000);
	}
	return false;
}

/* Commits a w x h buffer of one color with the given buffer scale and
 * waits for the frame app_server would get. */
static bool
commit_scaled(struct conn *c, struct toplevel *t, int w, int h,
	int32_t bufferScale, uint32_t color)
{
	int before = atomic_load(&sSetBufferCount);
	wl_surface_set_buffer_scale(t->surface, bufferScale);
	attach_color(c, t->surface, w, h, color);
	wl_surface_commit(t->surface);
	for (int i = 0; i < 200 && atomic_load(&sSetBufferCount) == before;
			i++) {
		if (!settle(c))
			return false;
		usleep(2000);
	}
	return atomic_load(&sSetBufferCount) != before;
}

/* At output scale 2 app_server gets the window content at 2 pixels per
 * window coordinate unit: a client rendering at buffer scale 2 is copied
 * 1:1, a client at buffer scale 1 is upscaled into the same frame. The
 * window keeps its logical size (frame / scale). */
static void
test_compose_scale(struct conn *c, struct toplevel *t)
{
	if (!set_output_scale(c, 2.0f)) {
		result(ST_FAIL, "compose-scale", "output scale never became 2");
		return;
	}

	bool ok = commit_scaled(c, t, 128, 96, 2, COLOR_CHILD);
	pthread_mutex_lock(&sFrameLock);
	int32_t w = sFrameWidth, h = sFrameHeight;
	float scale = sFrameScale;
	pthread_mutex_unlock(&sFrameLock);
	uint32_t corner = frame_pixel(127, 95);
	result(ok && w == 128 && h == 96 && scale == 2 && corner == COLOR_CHILD
			? ST_PASS : ST_FAIL, "compose-scale-hidpi-buffer",
		"%s; frame %dx%d at scale %.2f, corner 0x%08x",
		ok ? "delivered" : "NOT delivered", w, h, scale, corner);

	ok = commit_scaled(c, t, 64, 48, 1, COLOR_ROOT);
	pthread_mutex_lock(&sFrameLock);
	w = sFrameWidth;
	h = sFrameHeight;
	scale = sFrameScale;
	pthread_mutex_unlock(&sFrameLock);
	corner = frame_pixel(127, 95);
	result(ok && w == 128 && h == 96 && scale == 2 && corner == COLOR_ROOT
			? ST_PASS : ST_FAIL, "compose-scale-lowdpi-buffer",
		"%s; frame %dx%d at scale %.2f, corner 0x%08x",
		ok ? "delivered" : "NOT delivered", w, h, scale, corner);

	set_output_scale(c, 1.0f);
	commit_scaled(c, t, 64, 48, 1, COLOR_ROOT);
}

/* Firefox draws its whole window into a subsurface and, on a scaled
 * output, gives that subsurface its size with wp_viewport.set_destination
 * rather than a buffer scale: a buffer twice the window's size in each
 * direction is shown at the window's size. Without wp_viewporter the
 * subsurface is as large as its buffer and only its top left quarter is
 * seen, twice too large. The buffer here is red on its left half and green
 * on its right: scaled into the 64x48 window, the window's right edge is
 * green. */
static void
test_compose_viewport(struct conn *c, struct toplevel *t)
{
	if (c->viewporter == NULL) {
		result(ST_FAIL, "compose-viewport-subsurface",
			"no wp_viewporter global");
		return;
	}

	const int w = 128, h = 96;
	size_t size = (size_t)w * h * 4;
	int fd = make_shm_fd(size);
	uint32_t *pixels = fd >= 0 ? mmap(NULL, size, PROT_READ | PROT_WRITE,
		MAP_SHARED, fd, 0) : MAP_FAILED;
	if (pixels == MAP_FAILED) {
		if (fd >= 0)
			close(fd);
		result(ST_BLOCKED, "compose-viewport-subsurface", "no shm");
		return;
	}
	for (int y = 0; y < h; y++) {
		for (int x = 0; x < w; x++)
			pixels[y * w + x] = x < w / 2 ? COLOR_ROOT : COLOR_CHILD;
	}
	munmap(pixels, size);
	struct wl_shm_pool *pool = wl_shm_create_pool(c->shm, fd,
		(int32_t)size);
	struct wl_buffer *buffer = wl_shm_pool_create_buffer(pool, 0, w, h,
		w * 4, WL_SHM_FORMAT_ARGB8888);
	wl_shm_pool_destroy(pool);
	close(fd);

	struct wl_surface *child = wl_compositor_create_surface(c->compositor);
	struct wl_subsurface *sub = wl_subcompositor_get_subsurface(
		c->subcompositor, child, t->surface);
	struct wp_viewport *viewport = wp_viewporter_get_viewport(
		c->viewporter, child);
	wp_viewport_set_destination(viewport, 64, 48);
	wl_surface_attach(child, buffer, 0, 0);
	wl_surface_damage(child, 0, 0, w, h);
	wl_surface_commit(child);
	attach_color(c, t->surface, 64, 48, COLOR_GRAND);
	wl_surface_commit(t->surface);
	bool ok = settle(c);

	uint32_t left = frame_pixel(4, 20), right = frame_pixel(60, 20);
	result(ok && left == COLOR_ROOT && right == COLOR_CHILD
			? ST_PASS : ST_FAIL, "compose-viewport-subsurface",
		"left 0x%08x right 0x%08x (expected red 0x%08x, then green "
		"0x%08x)", left, right, COLOR_ROOT, COLOR_CHILD);

	wp_viewport_destroy(viewport);
	wl_subsurface_destroy(sub);
	wl_surface_destroy(child);
	attach_color(c, t->surface, 64, 48, COLOR_ROOT);
	wl_surface_commit(t->surface);
	settle(c);
}

/* wp_fractional_scale_v1: at 150% a client that asks for its preferred
 * scale is told 1.5 (180/120), renders a buffer of exactly that many
 * pixels and gives the surface its size with a viewport. The window then
 * reaches app_server at that density, copied 1:1: a 64x48 window is a
 * 96x72 frame at scale 1.5, not a 2x frame for app_server to shrink. */
static uint32_t sPreferredScale;

static void
fractional_preferred_scale(void *data,
	struct wp_fractional_scale_v1 *fractional, uint32_t scale)
{
	(void)data;
	(void)fractional;
	sPreferredScale = scale;
}

static const struct wp_fractional_scale_v1_listener sFractionalListener = {
	.preferred_scale = fractional_preferred_scale,
};

static void
test_compose_fractional(struct conn *c, struct toplevel *t)
{
	if (c->fractional_scale_manager == NULL || c->viewporter == NULL) {
		result(ST_FAIL, "compose-fractional-scale",
			"no wp_fractional_scale_manager_v1 (or wp_viewporter) global");
		return;
	}

	struct wp_fractional_scale_v1 *fractional
		= wp_fractional_scale_manager_v1_get_fractional_scale(
			c->fractional_scale_manager, t->surface);
	wp_fractional_scale_v1_add_listener(fractional, &sFractionalListener,
		NULL);
	sPreferredScale = 0;
	bool ok = set_output_scale(c, 1.5f);
	for (int i = 0; ok && i < 100 && sPreferredScale != 180; i++) {
		ok = settle(c);
		usleep(2000);
	}
	result(ok && sPreferredScale == 180 ? ST_PASS : ST_FAIL,
		"compose-fractional-preferred-scale",
		"preferred scale %u/120 (expected 180)", sPreferredScale);

	struct wp_viewport *viewport = wp_viewporter_get_viewport(c->viewporter,
		t->surface);
	wp_viewport_set_destination(viewport, 64, 48);
	ok = commit_scaled(c, t, 96, 72, 1, COLOR_CHILD);
	pthread_mutex_lock(&sFrameLock);
	int32_t w = sFrameWidth, h = sFrameHeight;
	float scale = sFrameScale;
	pthread_mutex_unlock(&sFrameLock);
	uint32_t corner = frame_pixel(95, 71);
	result(ok && w == 96 && h == 72 && scale == 1.5f
			&& corner == COLOR_CHILD ? ST_PASS : ST_FAIL,
		"compose-fractional-scale",
		"%s; frame %dx%d at scale %.2f, corner 0x%08x (expected 96x72 at "
		"1.50, 0x%08x)", ok ? "delivered" : "NOT delivered", w, h, scale,
		corner, COLOR_CHILD);

	/* A size whose scaled width has a fraction above one half: 61 x 1.75
	 * is 106.75. The protocol leaves rounding to the client; Firefox
	 * truncates (106) where the window is composed 107 wide. The buffer
	 * must still be copied as it is, not resampled: a filtered copy
	 * blends the two colours along the boundary between them. */
	if (set_output_scale(c, 1.75f)) {
		const int bw = 106, bh = 84; /* 61 x 48 at 1.75: 106.75 x 84 */
		size_t size = (size_t)bw * bh * 4;
		int fd = make_shm_fd(size);
		uint32_t *pixels = fd >= 0 ? mmap(NULL, size,
			PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0) : MAP_FAILED;
		if (pixels != MAP_FAILED) {
			for (int y = 0; y < bh; y++) {
				for (int x = 0; x < bw; x++)
					pixels[y * bw + x] = x < 53 ? COLOR_ROOT : COLOR_CHILD;
			}
			munmap(pixels, size);
			struct wl_shm_pool *pool = wl_shm_create_pool(c->shm, fd,
				(int32_t)size);
			struct wl_buffer *buffer = wl_shm_pool_create_buffer(pool, 0,
				bw, bh, bw * 4, WL_SHM_FORMAT_ARGB8888);
			wl_shm_pool_destroy(pool);
			close(fd);

			int before = atomic_load(&sSetBufferCount);
			xdg_surface_set_window_geometry(t->xdg_surface, 0, 0, 61, 48);
			wp_viewport_set_destination(viewport, 61, 48);
			wl_surface_attach(t->surface, buffer, 0, 0);
			wl_surface_damage(t->surface, 0, 0, bw, bh);
			wl_surface_commit(t->surface);
			for (int i = 0; i < 200
					&& atomic_load(&sSetBufferCount) == before; i++) {
				settle(c);
				usleep(2000);
			}
			uint32_t left = frame_pixel(52, 10), right = frame_pixel(53, 10);
			uint32_t edge = frame_pixel(106, 10);
			pthread_mutex_lock(&sFrameLock);
			w = sFrameWidth;
			pthread_mutex_unlock(&sFrameLock);
			result(left == COLOR_ROOT && right == COLOR_CHILD
					&& edge == COLOR_CHILD ? ST_PASS : ST_FAIL,
				"compose-fractional-rounding",
				"frame %d wide; pixels 52, 53, 106: 0x%08x 0x%08x 0x%08x "
				"(expected 0x%08x 0x%08x 0x%08x, copied, not filtered)", w,
				left, right, edge, COLOR_ROOT, COLOR_CHILD, COLOR_CHILD);
			xdg_surface_set_window_geometry(t->xdg_surface, 0, 0, 64, 48);
		} else if (fd >= 0) {
			close(fd);
		}
	} else {
		result(ST_FAIL, "compose-fractional-rounding",
			"output scale never became 1.75");
	}

	wp_viewport_destroy(viewport);
	wp_fractional_scale_v1_destroy(fractional);
	set_output_scale(c, 1.0f);
	commit_scaled(c, t, 64, 48, 1, COLOR_ROOT);
}

/* A surface's opaque region hides what is under it, so Koan does not
 * compose that part of the surfaces below. Firefox shows its page in a
 * subsurface that declares itself opaque over the whole window; the root
 * under it is GTK's frame, drawn at the integer scale and resampled. At
 * 150% in fullscreen, composing that hidden root took 45 of the 50 ms
 * per frame and dropped half the frames of a 25 fps video (QEMU,
 * 2560x1440). The protocol leaves what is under an opaque region to the
 * compositor, which the test uses to see the skip: the child declares its
 * left half opaque but draws premultiplied green at alpha 0x80. Under the
 * opaque half the frame holds the child's pixel alone; on the right half
 * it is blended over the red root. At 1.5 the root is a 2x buffer, the
 * resampled case, and the opaque edge (32 units) falls on pixel 48. */
#define COLOR_HALF 0x80006600u
#define COLOR_HALF_OVER_ROOT 0xff656600u /* COLOR_HALF over COLOR_ROOT */

static void
test_compose_occluded(struct conn *c, struct toplevel *t)
{
	struct wl_surface *child = wl_compositor_create_surface(c->compositor);
	struct wl_subsurface *sub = wl_subcompositor_get_subsurface(
		c->subcompositor, child, t->surface);
	struct wl_region *opaque = wl_compositor_create_region(c->compositor);
	wl_region_add(opaque, 0, 0, 32, 48);
	wl_surface_set_opaque_region(child, opaque);
	wl_region_destroy(opaque);
	attach_color(c, child, 64, 48, COLOR_HALF);
	wl_surface_commit(child);

	static const struct { float scale; int32_t bufferScale; int32_t edge;
		const char *name; } kCases[] = {
		{ 1.0f, 1, 32, "compose-occluded" },
		{ 1.5f, 2, 48, "compose-occluded-fractional" },
	};
	for (size_t i = 0; i < sizeof(kCases) / sizeof(kCases[0]); i++) {
		bool ok = set_output_scale(c, kCases[i].scale)
			&& commit_scaled(c, t, 64 * kCases[i].bufferScale,
				48 * kCases[i].bufferScale, kCases[i].bufferScale,
				COLOR_ROOT);
		const int32_t edge = kCases[i].edge;
		uint32_t first = frame_pixel(0, 10);
		uint32_t inside = frame_pixel(edge - 1, 10);
		uint32_t outside = frame_pixel(edge, 10);
		result(ok && first == COLOR_HALF && inside == COLOR_HALF
				&& outside == COLOR_HALF_OVER_ROOT ? ST_PASS : ST_FAIL,
			kCases[i].name, "%s; x 0 0x%08x, x %d 0x%08x (expected "
			"0x%08x), x %d 0x%08x (expected 0x%08x)",
			ok ? "delivered" : "NOT delivered", first, edge - 1, inside,
			COLOR_HALF, edge, outside, COLOR_HALF_OVER_ROOT);
	}

	wl_subsurface_destroy(sub);
	wl_surface_destroy(child);
	set_output_scale(c, 1.0f);
	commit_scaled(c, t, 64, 48, 1, COLOR_ROOT);
}

/* wp_viewport.set_destination sizes a surface independently of its
 * buffer: a 4 x 4 buffer can cover a 1000000 x 10 surface, and Koan
 * composes the window box at that size. Without a limit app_server
 * allocated and imported the full frame on every commit (up to 2 GiB at
 * once). An oversized box is not delivered; a normal one afterwards is. */
static void
test_compose_oversize(struct conn *c, struct toplevel *t)
{
	if (c->viewporter == NULL) {
		result(ST_BLOCKED, "compose-oversize", "no wp_viewporter global");
		return;
	}
	static const struct { int32_t w, h; const char *name; } kCases[] = {
		{ 1000000, 10, "compose-oversize-wide" },
		{ 10, 1000000, "compose-oversize-tall" },
	};
	struct wp_viewport *viewport = wp_viewporter_get_viewport(
		c->viewporter, t->surface);
	for (size_t i = 0; i < sizeof(kCases) / sizeof(kCases[0]); i++) {
		wp_viewport_set_destination(viewport, kCases[i].w, kCases[i].h);
		xdg_surface_set_window_geometry(t->xdg_surface, 0, 0, kCases[i].w,
			kCases[i].h);
		bool delivered = commit_scaled(c, t, 4, 4, 1, COLOR_ROOT);
		pthread_mutex_lock(&sFrameLock);
		int32_t w = sFrameWidth, h = sFrameHeight;
		pthread_mutex_unlock(&sFrameLock);
		result(!delivered ? ST_PASS : ST_FAIL, kCases[i].name,
			"%dx%d surface: %s (frame %dx%d)", kCases[i].w, kCases[i].h,
			delivered ? "frame delivered" : "refused", w, h);
	}

	wp_viewport_destroy(viewport);
	xdg_surface_set_window_geometry(t->xdg_surface, 0, 0, 64, 48);
	bool delivered = commit_scaled(c, t, 64, 48, 1, COLOR_CHILD);
	pthread_mutex_lock(&sFrameLock);
	int32_t w = sFrameWidth, h = sFrameHeight;
	pthread_mutex_unlock(&sFrameLock);
	result(delivered && w == 64 && h == 48 ? ST_PASS : ST_FAIL,
		"compose-oversize-recovers", "%s; frame %dx%d (expected 64x48)",
		delivered ? "delivered" : "NOT delivered", w, h);
}

static int
run_compose(void)
{
	struct server s;
	if (!server_start(&s)) {
		result(ST_BLOCKED, "compose", "server start");
		server_stop(&s);
		return summary();
	}
	struct conn c;
	struct toplevel t;
	if (!conn_open(&c, s.socket) || c.wm_base == NULL
		|| !map_toplevel(&c, &t, COLOR_ROOT)) {
		result(ST_BLOCKED, "compose", "toplevel did not map");
		server_stop(&s);
		return summary();
	}
	test_surface_enter(&c, &t);
	test_frame_loop(&c, &t);
	test_frame_throttle(&c, &t);
	test_nested_sync(&c, &t);
	test_compose_scale(&c, &t);
	test_compose_viewport(&c, &t);
	test_compose_fractional(&c, &t);
	test_compose_occluded(&c, &t);
	test_compose_oversize(&c, &t);

	struct conn after;
	bool alive = conn_open(&after, s.socket);
	conn_close(&after);
	result(alive ? ST_PASS : ST_FAIL, "compose-server-alive",
		alive ? "new client served" : "new client failed");
	conn_close(&c);
	server_stop(&s);
	return summary();
}

/* ---- xdg: popups --------------------------------------------------- */

struct popup {
	struct wl_surface *surface;
	struct xdg_surface *xdg_surface;
	struct xdg_popup *xdg_popup;
	uint32_t serial;
	bool configured;
	bool popup_configured;
	int32_t x, y, width, height;
	bool done;
};

static void
popup_surface_configure(void *data, struct xdg_surface *xdg_surface,
	uint32_t serial)
{
	(void)xdg_surface;
	struct popup *p = data;
	p->serial = serial;
	p->configured = true;
}

static const struct xdg_surface_listener sPopupSurfaceListener = {
	.configure = popup_surface_configure,
};

static void
popup_configure(void *data, struct xdg_popup *xdg_popup, int32_t x,
	int32_t y, int32_t width, int32_t height)
{
	(void)xdg_popup;
	struct popup *p = data;
	p->popup_configured = true;
	p->x = x;
	p->y = y;
	p->width = width;
	p->height = height;
}

static void
popup_done(void *data, struct xdg_popup *xdg_popup)
{
	(void)xdg_popup;
	((struct popup *)data)->done = true;
}

static void
popup_repositioned(void *data, struct xdg_popup *xdg_popup, uint32_t token)
{
	(void)data;
	(void)xdg_popup;
	(void)token;
}

static const struct xdg_popup_listener sPopupListener = {
	.configure = popup_configure,
	.popup_done = popup_done,
	.repositioned = popup_repositioned,
};

static void
popup_create(struct conn *c, struct toplevel *parent, struct popup *p)
{
	memset(p, 0, sizeof(*p));
	struct xdg_positioner *positioner
		= xdg_wm_base_create_positioner(c->wm_base);
	xdg_positioner_set_size(positioner, 20, 10);
	xdg_positioner_set_anchor_rect(positioner, 4, 4, 10, 10);
	xdg_positioner_set_anchor(positioner, XDG_POSITIONER_ANCHOR_BOTTOM_RIGHT);
	xdg_positioner_set_gravity(positioner,
		XDG_POSITIONER_GRAVITY_BOTTOM_RIGHT);
	p->surface = wl_compositor_create_surface(c->compositor);
	p->xdg_surface = xdg_wm_base_get_xdg_surface(c->wm_base, p->surface);
	xdg_surface_add_listener(p->xdg_surface, &sPopupSurfaceListener, p);
	p->xdg_popup = xdg_surface_get_popup(p->xdg_surface,
		parent->xdg_surface, positioner);
	xdg_popup_add_listener(p->xdg_popup, &sPopupListener, p);
	xdg_positioner_destroy(positioner);
	wl_surface_commit(p->surface);
}

/* ---- data: clipboard selection ---------------------------------------- */

#define SELECTION_MIME "text/plain;charset=utf-8"

/* One client's view of the seat: keyboard focus and the selection. */
struct data_client {
	struct conn c;
	struct toplevel t;
	struct wlr_xdg_toplevel *server_toplevel; /* key for focus commands */
	struct wl_keyboard *keyboard;
	struct wl_data_device *device;
	uint32_t enter_serial;
	bool focused;
	struct wl_data_offer *pending_offer;      /* introduced, mimes coming */
	bool pending_has_mime;
	struct wl_data_offer *selection;          /* current selection offer */
	bool selection_has_mime;
	int selection_events;
};

static void
dk_keymap(void *data, struct wl_keyboard *k, uint32_t format, int32_t fd,
	uint32_t size)
{
	(void)data;
	(void)k;
	(void)format;
	(void)size;
	close(fd);
}

static void
dk_enter(void *data, struct wl_keyboard *k, uint32_t serial,
	struct wl_surface *surface, struct wl_array *keys)
{
	(void)k;
	(void)surface;
	(void)keys;
	struct data_client *d = data;
	d->enter_serial = serial;
	d->focused = true;
}

static void
dk_leave(void *data, struct wl_keyboard *k, uint32_t serial,
	struct wl_surface *surface)
{
	(void)k;
	(void)serial;
	(void)surface;
	((struct data_client *)data)->focused = false;
}

static void
dk_key(void *data, struct wl_keyboard *k, uint32_t serial, uint32_t time,
	uint32_t key, uint32_t state)
{
	(void)data;
	(void)k;
	(void)serial;
	(void)time;
	(void)key;
	(void)state;
}

static void
dk_modifiers(void *data, struct wl_keyboard *k, uint32_t serial,
	uint32_t depressed, uint32_t latched, uint32_t locked, uint32_t group)
{
	(void)data;
	(void)k;
	(void)serial;
	(void)depressed;
	(void)latched;
	(void)locked;
	(void)group;
}

static void
dk_repeat_info(void *data, struct wl_keyboard *k, int32_t rate,
	int32_t delay)
{
	(void)data;
	(void)k;
	(void)rate;
	(void)delay;
}

static const struct wl_keyboard_listener sDataKeyboardListener = {
	.keymap = dk_keymap,
	.enter = dk_enter,
	.leave = dk_leave,
	.key = dk_key,
	.modifiers = dk_modifiers,
	.repeat_info = dk_repeat_info,
};

static void
offer_offer(void *data, struct wl_data_offer *offer, const char *mime)
{
	struct data_client *d = data;
	if (strcmp(mime, SELECTION_MIME) != 0)
		return;
	if (offer == d->pending_offer)
		d->pending_has_mime = true;
	if (offer == d->selection)
		d->selection_has_mime = true;
}

static void
offer_source_actions(void *data, struct wl_data_offer *offer, uint32_t a)
{
	(void)data;
	(void)offer;
	(void)a;
}

static void
offer_action(void *data, struct wl_data_offer *offer, uint32_t a)
{
	(void)data;
	(void)offer;
	(void)a;
}

static const struct wl_data_offer_listener sOfferListener = {
	.offer = offer_offer,
	.source_actions = offer_source_actions,
	.action = offer_action,
};

static void
dd_data_offer(void *data, struct wl_data_device *device,
	struct wl_data_offer *offer)
{
	(void)device;
	struct data_client *d = data;
	d->pending_offer = offer;
	d->pending_has_mime = false;
	wl_data_offer_add_listener(offer, &sOfferListener, d);
}

static void
dd_enter(void *data, struct wl_data_device *device, uint32_t serial,
	struct wl_surface *surface, wl_fixed_t x, wl_fixed_t y,
	struct wl_data_offer *offer)
{
	(void)data;
	(void)device;
	(void)serial;
	(void)surface;
	(void)x;
	(void)y;
	(void)offer;
}

static void
dd_leave(void *data, struct wl_data_device *device)
{
	(void)data;
	(void)device;
}

static void
dd_motion(void *data, struct wl_data_device *device, uint32_t time,
	wl_fixed_t x, wl_fixed_t y)
{
	(void)data;
	(void)device;
	(void)time;
	(void)x;
	(void)y;
}

static void
dd_drop(void *data, struct wl_data_device *device)
{
	(void)data;
	(void)device;
}

static void
dd_selection(void *data, struct wl_data_device *device,
	struct wl_data_offer *offer)
{
	(void)device;
	struct data_client *d = data;
	if (d->selection != NULL && d->selection != offer)
		wl_data_offer_destroy(d->selection);
	d->selection = offer;
	d->selection_has_mime = offer != NULL && offer == d->pending_offer
		&& d->pending_has_mime;
	d->selection_events++;
}

static const struct wl_data_device_listener sDataDeviceListener = {
	.data_offer = dd_data_offer,
	.enter = dd_enter,
	.leave = dd_leave,
	.motion = dd_motion,
	.drop = dd_drop,
	.selection = dd_selection,
};

static void
source_target(void *data, struct wl_data_source *s, const char *mime)
{
	(void)data;
	(void)s;
	(void)mime;
}

static void
source_send(void *data, struct wl_data_source *s, const char *mime,
	int32_t fd)
{
	(void)data;
	(void)s;
	(void)mime;
	close(fd);
}

static void
source_cancelled(void *data, struct wl_data_source *s)
{
	(void)data;
	(void)s;
}

static const struct wl_data_source_listener sSourceListener = {
	.target = source_target,
	.send = source_send,
	.cancelled = source_cancelled,
};

static bool
data_client_open(struct data_client *d, const char *socket, uint32_t color)
{
	memset(d, 0, sizeof(*d));
	atomic_store(&sCreatedToplevel, NULL);
	if (!conn_open(&d->c, socket) || d->c.wm_base == NULL
		|| d->c.seat == NULL || d->c.data_device_manager == NULL
		|| (d->c.seat_caps & WL_SEAT_CAPABILITY_KEYBOARD) == 0)
		return false;
	d->keyboard = wl_seat_get_keyboard(d->c.seat);
	wl_keyboard_add_listener(d->keyboard, &sDataKeyboardListener, d);
	d->device = wl_data_device_manager_get_data_device(
		d->c.data_device_manager, d->c.seat);
	wl_data_device_add_listener(d->device, &sDataDeviceListener, d);
	if (!map_toplevel(&d->c, &d->t, color))
		return false;
	d->server_toplevel = atomic_load(&sCreatedToplevel);
	return d->server_toplevel != NULL;
}

/* app_server (de)activating a native window. */
static void
push_focus(struct wlr_xdg_toplevel *toplevel, bool focused)
{
	union koan_cmd_payload focus = {0};
	focus.keyboard_focus.toplevel = toplevel;
	focus.keyboard_focus.focused = focused;
	koan_queue_push(koan_adapter_queue(), KOAN_CMD_KEYBOARD_FOCUS, &focus,
		NULL);
}

static bool
settle_both(struct data_client *a, struct data_client *b)
{
	bool ok = true;
	for (int i = 0; i < 3; i++)
		ok = settle(&a->c) && settle(&b->c) && ok;
	return ok;
}

/* The source copies: a selection with a serial from its own
 * wl_keyboard.enter, "the event that triggered this request". */
static struct wl_data_source *
copy(struct data_client *source)
{
	struct wl_data_source *s = wl_data_device_manager_create_data_source(
		source->c.data_device_manager);
	wl_data_source_add_listener(s, &sSourceListener, NULL);
	wl_data_source_offer(s, SELECTION_MIME);
	wl_data_device_set_selection(source->device, s, source->enter_serial);
	return s;
}

static int
run_data(void)
{
	struct server s;
	if (!server_start(&s)) {
		result(ST_BLOCKED, "data", "server start");
		server_stop(&s);
		return summary();
	}

	/* 1. Source copies while focused; then the sink gets focus and must be
	 * offered the selection (WLCS CopyCutPaste
	 * given_source_has_offered_when_sink_gets_focus_it_sees_offer). */
	struct data_client src, sink;
	if (!data_client_open(&src, s.socket, COLOR_ROOT)) {
		result(ST_BLOCKED, "selection-offered-on-focus", "source setup");
		server_stop(&s);
		return summary();
	}
	push_focus(src.server_toplevel, true);
	settle(&src.c);
	bool ok = src.focused && src.enter_serial != 0;
	if (!ok) {
		result(ST_BLOCKED, "selection-offered-on-focus",
			"the source never got keyboard focus");
		server_stop(&s);
		return summary();
	}
	struct wl_data_source *selection = copy(&src);
	settle(&src.c);

	if (!data_client_open(&sink, s.socket, COLOR_CHILD)) {
		result(ST_BLOCKED, "selection-offered-on-focus", "sink setup");
		server_stop(&s);
		return summary();
	}
	push_focus(src.server_toplevel, false);
	push_focus(sink.server_toplevel, true);
	ok = settle_both(&src, &sink);
	result(ok && sink.focused && sink.selection != NULL
			&& sink.selection_has_mime ? ST_PASS : ST_FAIL,
		"selection-offered-on-focus",
		"sink focused %d, selection offer %s, mime %s",
		sink.focused, sink.selection != NULL ? "received" : "MISSING",
		sink.selection_has_mime ? SELECTION_MIME : "MISSING");

	/* 2. The sink keeps focus; the source copies again with its old (still
	 * valid) enter serial: the focused sink must see the new selection at
	 * once (WLCS given_sink_has_focus_when_source_makes_offer_...). */
	int before = sink.selection_events;
	struct wl_data_source *second = copy(&src);
	ok = settle_both(&src, &sink);
	result(ok && sink.selection_events > before && sink.selection != NULL
			&& sink.selection_has_mime ? ST_PASS : ST_FAIL,
		"selection-reaches-focused-client",
		"%d new selection event(s) at the focused sink, mime %s",
		sink.selection_events - before,
		sink.selection_has_mime ? SELECTION_MIME : "MISSING");

	/* 3. wayland.xml: the serial must be one the client got from an
	 * event. Serial 0 never is; the selection must not change. */
	before = sink.selection_events;
	struct wl_data_source *bogus = wl_data_device_manager_create_data_source(
		src.c.data_device_manager);
	wl_data_source_offer(bogus, SELECTION_MIME);
	wl_data_device_set_selection(src.device, bogus, 0);
	ok = settle_both(&src, &sink);
	result(ok && sink.selection_events == before ? ST_PASS : ST_FAIL,
		"selection-invalid-serial-ignored",
		"%d selection event(s) after set_selection with serial 0",
		sink.selection_events - before);

	(void)selection;
	(void)second;
	conn_close(&sink.c);
	conn_close(&src.c);
	server_stop(&s);
	return summary();
}

/* ---- input: window box vs pointer coordinates -------------------------- */

struct pointer_state {
	struct wl_surface *focus;
	double x, y;
	int enters;
	int buttons;                /* wl_pointer.button events */
	uint32_t button;            /* the last one */
	uint32_t button_serial;     /* its serial (xdg_popup.grab needs one) */
};

static void
ptr_enter(void *data, struct wl_pointer *p, uint32_t serial,
	struct wl_surface *surface, wl_fixed_t x, wl_fixed_t y)
{
	(void)p;
	(void)serial;
	struct pointer_state *st = data;
	st->focus = surface;
	st->x = wl_fixed_to_double(x);
	st->y = wl_fixed_to_double(y);
	st->enters++;
}

static void
ptr_leave(void *data, struct wl_pointer *p, uint32_t serial,
	struct wl_surface *surface)
{
	(void)p;
	(void)serial;
	(void)surface;
	((struct pointer_state *)data)->focus = NULL;
}

static void
ptr_motion(void *data, struct wl_pointer *p, uint32_t time, wl_fixed_t x,
	wl_fixed_t y)
{
	(void)p;
	(void)time;
	struct pointer_state *st = data;
	st->x = wl_fixed_to_double(x);
	st->y = wl_fixed_to_double(y);
}

static void
ptr_button(void *data, struct wl_pointer *p, uint32_t serial, uint32_t time,
	uint32_t button, uint32_t state)
{
	(void)p;
	(void)time;
	(void)state;
	struct pointer_state *st = data;
	st->buttons++;
	st->button = button;
	st->button_serial = serial;
}

static void
ptr_axis(void *data, struct wl_pointer *p, uint32_t time, uint32_t axis,
	wl_fixed_t value)
{
	(void)data;
	(void)p;
	(void)time;
	(void)axis;
	(void)value;
}

static void
ptr_frame(void *data, struct wl_pointer *p)
{
	(void)data;
	(void)p;
}

static void
ptr_axis_source(void *data, struct wl_pointer *p, uint32_t source)
{
	(void)data;
	(void)p;
	(void)source;
}

static void
ptr_axis_stop(void *data, struct wl_pointer *p, uint32_t time, uint32_t axis)
{
	(void)data;
	(void)p;
	(void)time;
	(void)axis;
}

static void
ptr_axis_discrete(void *data, struct wl_pointer *p, uint32_t axis,
	int32_t discrete)
{
	(void)data;
	(void)p;
	(void)axis;
	(void)discrete;
}

static const struct wl_pointer_listener sPointerListener = {
	.enter = ptr_enter,
	.leave = ptr_leave,
	.motion = ptr_motion,
	.button = ptr_button,
	.axis = ptr_axis,
	.frame = ptr_frame,
	.axis_source = ptr_axis_source,
	.axis_stop = ptr_axis_stop,
	.axis_discrete = ptr_axis_discrete,
};

/* Which surface has the keyboard, and the keys it got. */
struct kbd_state {
	struct wl_surface *focus;
	int keys;
	uint32_t last_key;
	struct wl_surface *last_key_focus;
};

static void
kb_keymap(void *data, struct wl_keyboard *k, uint32_t format, int32_t fd,
	uint32_t size)
{
	(void)data;
	(void)k;
	(void)format;
	(void)size;
	close(fd);
}

static void
kb_enter(void *data, struct wl_keyboard *k, uint32_t serial,
	struct wl_surface *surface, struct wl_array *keys)
{
	(void)k;
	(void)serial;
	(void)keys;
	((struct kbd_state *)data)->focus = surface;
}

static void
kb_leave(void *data, struct wl_keyboard *k, uint32_t serial,
	struct wl_surface *surface)
{
	(void)k;
	(void)serial;
	struct kbd_state *st = data;
	if (st->focus == surface)
		st->focus = NULL;
}

static void
kb_key(void *data, struct wl_keyboard *k, uint32_t serial, uint32_t time,
	uint32_t key, uint32_t state)
{
	(void)k;
	(void)serial;
	(void)time;
	(void)state;
	struct kbd_state *st = data;
	st->keys++;
	st->last_key = key;
	st->last_key_focus = st->focus;
}

static void
kb_modifiers(void *data, struct wl_keyboard *k, uint32_t serial,
	uint32_t depressed, uint32_t latched, uint32_t locked, uint32_t group)
{
	(void)data;
	(void)k;
	(void)serial;
	(void)depressed;
	(void)latched;
	(void)locked;
	(void)group;
}

static void
kb_repeat_info(void *data, struct wl_keyboard *k, int32_t rate,
	int32_t delay)
{
	(void)data;
	(void)k;
	(void)rate;
	(void)delay;
}

static const struct wl_keyboard_listener sKbdListener = {
	.keymap = kb_keymap,
	.enter = kb_enter,
	.leave = kb_leave,
	.key = kb_key,
	.modifiers = kb_modifiers,
	.repeat_info = kb_repeat_info,
};

/* app_server moving the pointer over its native window. */
static void
push_motion(struct wlr_xdg_toplevel *toplevel, double x, double y)
{
	union koan_cmd_payload in = {0};
	in.pointer_motion.toplevel = toplevel;
	in.pointer_motion.x = x;
	in.pointer_motion.y = y;
	koan_queue_push(koan_adapter_queue(), KOAN_CMD_POINTER_MOTION, &in,
		NULL);
}

/*
 * app_server's native window is the box koan_adapter.c delivers with
 * set_buffer, and it sends pointer positions relative to that box's
 * origin. Koan must map them back with the same box. A desynchronized
 * sub-surface that sticks out to the left and then unmaps itself shrinks
 * the box at once, while wlroots' effective xdg geometry only follows on
 * the toplevel's next own commit (WLCS SurfaceInputCombinations
 * input_falls_through_subsurface_when_unmapped).
 */
static int
run_input(void)
{
	struct server s;
	if (!server_start(&s)) {
		result(ST_BLOCKED, "input", "server start");
		server_stop(&s);
		return summary();
	}
	struct conn c;
	struct toplevel t;
	struct pointer_state ptr = {0};
	atomic_store(&sCreatedToplevel, NULL);
	if (!conn_open(&c, s.socket) || c.wm_base == NULL || c.seat == NULL
		|| !map_toplevel(&c, &t, COLOR_ROOT)) {
		result(ST_BLOCKED, "input", "toplevel did not map");
		server_stop(&s);
		return summary();
	}
	struct wlr_xdg_toplevel *key = atomic_load(&sCreatedToplevel);
	struct wl_pointer *pointer = wl_seat_get_pointer(c.seat);
	wl_pointer_add_listener(pointer, &sPointerListener, &ptr);

	struct wl_surface *child = wl_compositor_create_surface(c.compositor);
	struct wl_subsurface *sub = wl_subcompositor_get_subsurface(
		c.subcompositor, child, t.surface);
	wl_subsurface_set_position(sub, -20, 0);
	wl_subsurface_set_desync(sub);
	attach_color(&c, child, 16, 16, COLOR_GRAND);
	wl_surface_commit(child);
	wl_surface_commit(t.surface);
	settle(&c);
	int32_t wide = sFrameWidth;

	/* The sub-surface unmaps itself; the root does not commit. */
	wl_surface_attach(child, NULL, 0, 0);
	wl_surface_commit(child);
	bool ok = settle(&c);
	int32_t narrow = sFrameWidth;
	result(ok && wide == 84 && narrow == 64 ? ST_PASS : ST_FAIL,
		"window-box-follows-unmapped-subsurface",
		"native window width %d with the sub-surface, %d after it "
		"unmapped (expected 84, 64)", wide, narrow);

	push_motion(key, 10, 10);
	ok = settle(&c);
	result(ok && ptr.focus == t.surface && ptr.x == 10 && ptr.y == 10
			? ST_PASS : ST_FAIL, "pointer-coordinates-match-window-box",
		"window point (10,10) entered %s at (%.0f,%.0f) (expected the "
		"root at 10,10)", ptr.focus == t.surface ? "the root"
			: ptr.focus == NULL ? "nothing" : "another surface",
		ptr.x, ptr.y);

	/* Restack: two sub-surfaces at (0,0); the second is created on top,
	 * then placed below the first (WLCS SubsurfaceTest place_below_simple).
	 * wayland.xml (wl_subsurface.place_below): "put back just below the
	 * reference surface", so the first one must get the pointer, and only
	 * it: a single motion, a single enter. */
	struct wl_surface *first = wl_compositor_create_surface(c.compositor);
	struct wl_subsurface *first_sub = wl_subcompositor_get_subsurface(
		c.subcompositor, first, t.surface);
	attach_color(&c, first, 32, 32, COLOR_CHILD);
	wl_surface_commit(first);
	struct wl_surface *second = wl_compositor_create_surface(c.compositor);
	struct wl_subsurface *second_sub = wl_subcompositor_get_subsurface(
		c.subcompositor, second, t.surface);
	attach_color(&c, second, 32, 32, COLOR_GRAND);
	wl_surface_commit(second);
	wl_surface_commit(t.surface);
	settle(&c);
	wl_subsurface_place_below(second_sub, first);
	wl_surface_commit(second);
	wl_surface_commit(first);
	wl_surface_commit(t.surface);
	settle(&c);
	uint32_t shown = frame_pixel(5, 5);
	push_motion(key, 40, 40);       /* off both, onto the root */
	settle(&c);
	int enters = ptr.enters;
	push_motion(key, 5, 5);
	ok = settle(&c);
	result(ok && ptr.focus == first && ptr.enters == enters + 1
			&& shown == COLOR_CHILD ? ST_PASS : ST_FAIL,
		"pointer-follows-place-below",
		"window shows 0x%08x at (5,5) (expected the first sub-surface "
		"0x%08x); pointer entered %s with %d enter(s)", shown, COLOR_CHILD,
		ptr.focus == first ? "the first sub-surface"
			: ptr.focus == second ? "the second sub-surface"
			: "another surface", ptr.enters - enters);
	(void)first_sub;

	/* A popup (menu) over the window: app_server's popup window sends
	 * positions in the toplevel's content coordinates, where Koan's
	 * hit-test finds the popup on top. A click there must reach the
	 * popup: Nautilus's menu items highlighted but never activated. */
	{
		struct popup m;
		popup_create(&c, &t, &m);
		ok = settle(&c) && m.configured;
		if (ok) {
			xdg_surface_ack_configure(m.xdg_surface, m.serial);
			attach_color(&c, m.surface, 20, 10, COLOR_GRAND);
			wl_surface_commit(m.surface);
			ok = settle(&c);
		}
		push_motion(key, m.x + 5, m.y + 5);
		ok = ok && settle(&c);
		bool on_popup = ptr.focus == m.surface;
		int buttons = ptr.buttons;
		union koan_cmd_payload press = {0};
		press.pointer_button.toplevel = key;
		press.pointer_button.button = 0x110;
		press.pointer_button.pressed = true;
		koan_queue_push(koan_adapter_queue(), KOAN_CMD_POINTER_BUTTON, &press,
			NULL);
		press.pointer_button.pressed = false;
		koan_queue_push(koan_adapter_queue(), KOAN_CMD_POINTER_BUTTON, &press,
			NULL);
		ok = ok && settle(&c);
		result(ok && on_popup && ptr.buttons == buttons + 2 ? ST_PASS
				: ST_FAIL, "pointer-button-reaches-popup",
			"pointer %s the popup; %d button event(s) (expected 2)",
			on_popup ? "on" : "NOT on", ptr.buttons - buttons);
		xdg_popup_destroy(m.xdg_popup);
		xdg_surface_destroy(m.xdg_surface);
		wl_surface_destroy(m.surface);
		settle(&c);
	}

	/* Menus and the keyboard (xdg_popup.grab: the topmost grabbing popup
	 * "should be given keyboard focus"). wlroots' popup grab swallows
	 * focus changes, so Koan must hand the keys over itself: arrow keys
	 * in a GTK menu went to the window behind it. */
	if ((c.seat_caps & WL_SEAT_CAPABILITY_KEYBOARD) == 0) {
		result(ST_BLOCKED, "popup-keyboard", "seat has no keyboard");
	} else {
		struct kbd_state kb = {0};
		struct wl_keyboard *keyboard = wl_seat_get_keyboard(c.seat);
		wl_keyboard_add_listener(keyboard, &sKbdListener, &kb);
		union koan_cmd_payload focus = {0};
		focus.keyboard_focus.toplevel = key;
		focus.keyboard_focus.focused = 1;
		koan_queue_push(koan_adapter_queue(), KOAN_CMD_KEYBOARD_FOCUS, &focus,
			NULL);
		union koan_cmd_payload press = {0};
		press.pointer_button.toplevel = key;
		press.pointer_button.button = 0x110;
		press.pointer_button.pressed = true;
		push_motion(key, 30, 30);
		koan_queue_push(koan_adapter_queue(), KOAN_CMD_POINTER_BUTTON, &press,
			NULL);
		settle(&c);
		bool window_focused = kb.focus == t.surface;

		struct popup m;
		popup_create(&c, &t, &m);
		xdg_popup_grab(m.xdg_popup, c.seat, ptr.button_serial);
		ok = settle(&c) && m.configured;
		if (ok) {
			xdg_surface_ack_configure(m.xdg_surface, m.serial);
			attach_color(&c, m.surface, 20, 10, COLOR_GRAND);
			wl_surface_commit(m.surface);
			ok = settle(&c) && !m.done;
		}
		result(ok && window_focused && kb.focus == m.surface ? ST_PASS
				: ST_FAIL, "popup-keyboard-to-grabbing-popup",
			"window had the keyboard: %s; after the menu mapped it is on %s",
			window_focused ? "yes" : "NO",
			kb.focus == m.surface ? "the menu"
				: kb.focus == t.surface ? "the window" : "nothing");

		union koan_cmd_payload k = {0};
		k.keyboard_key.toplevel = key;
		k.keyboard_key.key = 108;           /* KEY_DOWN */
		k.keyboard_key.pressed = true;
		koan_queue_push(koan_adapter_queue(), KOAN_CMD_KEYBOARD_KEY, &k, NULL);
		k.keyboard_key.pressed = false;
		koan_queue_push(koan_adapter_queue(), KOAN_CMD_KEYBOARD_KEY, &k, NULL);
		int keys = kb.keys;
		settle(&c);
		result(kb.keys == keys + 2 && kb.last_key == 108
				&& kb.last_key_focus == m.surface ? ST_PASS : ST_FAIL,
			"popup-keyboard-keys-reach-popup",
			"%d key event(s), last on %s", kb.keys - keys,
			kb.last_key_focus == m.surface ? "the menu"
				: kb.last_key_focus == t.surface ? "the window" : "nothing");

		xdg_popup_destroy(m.xdg_popup);
		xdg_surface_destroy(m.xdg_surface);
		wl_surface_destroy(m.surface);
		settle(&c);
		result(kb.focus == t.surface ? ST_PASS : ST_FAIL,
			"popup-keyboard-back-to-parent",
			"after the menu closed the keyboard is on %s",
			kb.focus == t.surface ? "the window" : "something else");
		press.pointer_button.pressed = false;
		koan_queue_push(koan_adapter_queue(), KOAN_CMD_POINTER_BUTTON, &press,
			NULL);
		settle(&c);
	}

	/* app_server saw a click outside every client surface (a window
	 * frame, a native window, the desktop): KOAN_CMD_DISMISS_POPUPS ends
	 * the grabs, so menus close; tooltips (no grab) stay. */
	{
		union koan_cmd_payload press = {0};
		press.pointer_button.toplevel = key;
		press.pointer_button.button = 0x110;
		press.pointer_button.pressed = true;
		push_motion(key, 30, 30);
		koan_queue_push(koan_adapter_queue(), KOAN_CMD_POINTER_BUTTON, &press,
			NULL);
		settle(&c);
		struct popup menu, tip;
		popup_create(&c, &t, &menu);
		xdg_popup_grab(menu.xdg_popup, c.seat, ptr.button_serial);
		popup_create(&c, &t, &tip);
		ok = settle(&c) && menu.configured && tip.configured;
		if (ok) {
			xdg_surface_ack_configure(menu.xdg_surface, menu.serial);
			attach_color(&c, menu.surface, 20, 10, COLOR_GRAND);
			wl_surface_commit(menu.surface);
			xdg_surface_ack_configure(tip.xdg_surface, tip.serial);
			attach_color(&c, tip.surface, 20, 10, COLOR_CHILD);
			wl_surface_commit(tip.surface);
			ok = settle(&c) && !menu.done && !tip.done;
		}
		union koan_cmd_payload none = {0};
		ok = ok && koan_queue_push(koan_adapter_queue(),
			KOAN_CMD_DISMISS_POPUPS, &none, NULL) != NULL && settle(&c);
		result(ok && menu.done && !tip.done ? ST_PASS : ST_FAIL,
			"click-outside-dismisses-grabbing-popups",
			"menu popup_done %s, tooltip popup_done %s",
			menu.done ? "received" : "MISSING",
			tip.done ? "received (wrong)" : "not sent");
		xdg_popup_destroy(tip.xdg_popup);
		xdg_surface_destroy(tip.xdg_surface);
		wl_surface_destroy(tip.surface);
		xdg_popup_destroy(menu.xdg_popup);
		xdg_surface_destroy(menu.xdg_surface);
		wl_surface_destroy(menu.surface);
		press.pointer_button.pressed = false;
		koan_queue_push(koan_adapter_queue(), KOAN_CMD_POINTER_BUTTON, &press,
			NULL);
		settle(&c);
	}

	/* A menu holding the pointer grab (xdg_popup.grab, with the press's
	 * serial, as GTK does) while app_server quits: wlroots destroyed the
	 * grab twice when koan_server_destroy() destroyed the seat, and
	 * app_server crashed on every session end with a menu open. The
	 * client stays connected so the server tears the popup down. */
	{
		struct popup m;
		union koan_cmd_payload press = {0};
		press.pointer_button.toplevel = key;
		press.pointer_button.button = 0x111;
		press.pointer_button.pressed = true;
		push_motion(key, 30, 30);
		koan_queue_push(koan_adapter_queue(), KOAN_CMD_POINTER_BUTTON, &press,
			NULL);
		settle(&c);
		popup_create(&c, &t, &m);
		xdg_popup_grab(m.xdg_popup, c.seat, ptr.button_serial);
		ok = settle(&c) && m.configured && !m.done;
		if (ok) {
			xdg_surface_ack_configure(m.xdg_surface, m.serial);
			attach_color(&c, m.surface, 20, 10, COLOR_GRAND);
			wl_surface_commit(m.surface);
			ok = settle(&c) && !m.done;
		}
		/* server_stop() crashed here. */
		server_stop(&s);
		result(ok ? ST_PASS : ST_BLOCKED, "shutdown-with-grabbing-popup",
			ok ? "server shut down with a grabbing popup mapped"
				: "the popup did not get the grab");
		conn_close(&c);
		return summary();
	}
}

static int
run_xdg(void)
{
	struct server s;
	if (!server_start(&s)) {
		result(ST_BLOCKED, "xdg", "server start");
		server_stop(&s);
		return summary();
	}
	struct conn c;
	struct toplevel t;
	if (!conn_open(&c, s.socket) || c.wm_base == NULL
		|| !map_toplevel(&c, &t, COLOR_ROOT)) {
		result(ST_BLOCKED, "xdg", "toplevel did not map");
		server_stop(&s);
		return summary();
	}
	/* Later cases map other toplevels; keep this one's key. */
	struct wlr_xdg_toplevel *main_toplevel = atomic_load(&sCreatedToplevel);

	/* xdg-shell.xml (xdg_toplevel.set_/unset_maximized and
	 * set_/unset_fullscreen): "the compositor will respond by emitting a
	 * configure event". The request goes to the window manager
	 * (koan_window_callbacks.request_state; app_server zooms or goes
	 * fullscreen) and its answer comes back as that configure. */
	static const struct {
		const char *name;
		int request;              /* 0 max, 1 unmax, 2 fs, 3 unfs */
		bool maximized, fullscreen;
		int32_t width;            /* 800 when the state is entered */
	} kStateRequests[] = {
		{ "toplevel-set-maximized-granted", 0, true, false, 800 },
		{ "toplevel-unset-maximized-granted", 1, false, false, -1 },
		{ "toplevel-set-fullscreen-granted", 2, false, true, 800 },
		{ "toplevel-unset-fullscreen-granted", 3, false, false, -1 },
	};
	for (size_t i = 0; i < sizeof(kStateRequests) / sizeof(*kStateRequests);
		i++) {
		int before = t.configures;
		int asked = atomic_load(&sStateRequests);
		switch (kStateRequests[i].request) {
			case 0: xdg_toplevel_set_maximized(t.xdg_toplevel); break;
			case 1: xdg_toplevel_unset_maximized(t.xdg_toplevel); break;
			case 2: xdg_toplevel_set_fullscreen(t.xdg_toplevel, NULL); break;
			default: xdg_toplevel_unset_fullscreen(t.xdg_toplevel); break;
		}
		bool ok = wait_for_count(&c, &t.configures, before + 1);
		if (t.configures > before) {
			xdg_surface_ack_configure(t.xdg_surface, t.configure_serial);
			wl_surface_commit(t.surface);
			ok = ok && settle(&c);
		}
		ok = ok && atomic_load(&sStateRequests) == asked + 1
			&& t.maximized == kStateRequests[i].maximized
			&& t.fullscreen == kStateRequests[i].fullscreen
			&& (kStateRequests[i].width < 0
				|| t.width == kStateRequests[i].width);
		result(ok ? ST_PASS : ST_FAIL, kStateRequests[i].name,
			"window manager asked %d time(s); configure: %dx%d "
			"maximized=%d fullscreen=%d",
			atomic_load(&sStateRequests) - asked, t.width, t.height,
			t.maximized, t.fullscreen);
	}

	/* A client may destroy its toplevel while app_server is still
	 * creating the native window (the handle arrives later with
	 * WINDOW_READY). That window must still be destroyed: left alive it
	 * stays on screen without a client, and app_server keeps it under the
	 * dead toplevel's address, which a new toplevel can get from malloc
	 * (its window was then refused as a "duplicate" and never shown, so
	 * WLCS's new windows never became active). */
	{
		static int late_window;
		struct toplevel o = {0};
		o.surface = wl_compositor_create_surface(c.compositor);
		o.xdg_surface = xdg_wm_base_get_xdg_surface(c.wm_base, o.surface);
		xdg_surface_add_listener(o.xdg_surface, &sXdgSurfaceListener, &o);
		o.xdg_toplevel = xdg_surface_get_toplevel(o.xdg_surface);
		wl_surface_commit(o.surface);
		bool ok = settle(&c) && o.configured;
		struct wlr_xdg_toplevel *before = atomic_load(&sCreatedToplevel);
		if (ok) {
			xdg_surface_ack_configure(o.xdg_surface, o.configure_serial);
			attach_color(&c, o.surface, 32, 32, COLOR_CHILD);
			wl_surface_commit(o.surface);
			ok = settle(&c);
		}
		struct wlr_xdg_toplevel *dead = atomic_load(&sCreatedToplevel);
		ok = ok && dead != before;   /* create_window was asked */
		xdg_toplevel_destroy(o.xdg_toplevel);
		xdg_surface_destroy(o.xdg_surface);
		wl_surface_destroy(o.surface);
		ok = ok && settle(&c);
		int destroyed = atomic_load(&sDestroyWindowCount);
		union koan_cmd_payload ready = {0};
		ready.window_ready.toplevel = dead;
		ready.window_ready.window = &late_window;
		ok = ok && koan_queue_push(koan_adapter_queue(),
			KOAN_CMD_WINDOW_READY, &ready, NULL) != NULL && settle(&c);
		int n = atomic_load(&sDestroyWindowCount) - destroyed;
		result(ok && n == 1 && atomic_load(&sDestroyedWindow) == &late_window
				? ST_PASS : ST_FAIL, "toplevel-destroyed-before-window-ready",
			"late window destroyed %d time(s)%s", n,
			ok ? "" : "; setup failed");
	}

	/* A client may ask to be maximized as soon as it is mapped, before
	 * app_server reported its native window (WINDOW_READY). The request
	 * belongs to the window manager: it must reach it once the window
	 * exists, not be answered with the current state (WLCS
	 * window_can_maximize_itself failed this way about half the time). */
	{
		static int pending_window;
		struct toplevel o = {0};
		o.surface = wl_compositor_create_surface(c.compositor);
		o.xdg_surface = xdg_wm_base_get_xdg_surface(c.wm_base, o.surface);
		xdg_surface_add_listener(o.xdg_surface, &sXdgSurfaceListener, &o);
		o.xdg_toplevel = xdg_surface_get_toplevel(o.xdg_surface);
		xdg_toplevel_add_listener(o.xdg_toplevel, &sToplevelListener, &o);
		wl_surface_commit(o.surface);
		bool ok = settle(&c) && o.configured;
		if (ok) {
			xdg_surface_ack_configure(o.xdg_surface, o.configure_serial);
			attach_color(&c, o.surface, 32, 32, COLOR_CHILD);
			wl_surface_commit(o.surface);
			ok = settle(&c);
		}
		int asked = atomic_load(&sStateRequests);
		xdg_toplevel_set_maximized(o.xdg_toplevel);
		ok = ok && settle(&c);
		int asked_before_ready = atomic_load(&sStateRequests) - asked;
		int configures = o.configures;
		union koan_cmd_payload ready = {0};
		ready.window_ready.toplevel = atomic_load(&sCreatedToplevel);
		ready.window_ready.window = &pending_window;
		ok = ok && koan_queue_push(koan_adapter_queue(),
			KOAN_CMD_WINDOW_READY, &ready, NULL) != NULL
			&& wait_for_count(&c, &o.configures, configures + 1);
		int asked_after_ready = atomic_load(&sStateRequests) - asked;
		result(ok && asked_before_ready == 0 && asked_after_ready == 1
				&& o.maximized && o.width == 800 ? ST_PASS : ST_FAIL,
			"toplevel-maximize-before-window-ready",
			"window manager asked %d time(s) before WINDOW_READY, %d after; "
			"configure %dx%d maximized=%d", asked_before_ready,
			asked_after_ready, o.width, o.height, o.maximized);
		xdg_toplevel_destroy(o.xdg_toplevel);
		xdg_surface_destroy(o.xdg_surface);
		wl_surface_destroy(o.surface);
		settle(&c);
	}

	/* xdg-shell.xml (xdg_toplevel.set_minimized): "Request that the
	 * compositor minimize your surface". It goes to the window manager
	 * (koan_window_callbacks.request_minimize; app_server minimizes the
	 * native window like its minimize button). No configure is due. */
	{
		int asked = atomic_load(&sMinimizeRequests);
		xdg_toplevel_set_minimized(t.xdg_toplevel);
		bool ok = settle(&c);
		int n = atomic_load(&sMinimizeRequests) - asked;
		result(ok && n == 1 ? ST_PASS : ST_FAIL, "toplevel-set-minimized",
			"window manager asked %d time(s)%s", n,
			ok ? "" : "; client disconnected");
	}

	/* Allowed order: the first configure has arrived, the client attaches,
	 * then acks and commits. Only an attach before any configure is an
	 * error. */
	{
		struct toplevel o = {0};
		o.surface = wl_compositor_create_surface(c.compositor);
		o.xdg_surface = xdg_wm_base_get_xdg_surface(c.wm_base, o.surface);
		xdg_surface_add_listener(o.xdg_surface, &sXdgSurfaceListener, &o);
		o.xdg_toplevel = xdg_surface_get_toplevel(o.xdg_surface);
		wl_surface_commit(o.surface);
		bool ok = settle(&c) && o.configured;
		if (ok) {
			attach_color(&c, o.surface, 32, 32, COLOR_CHILD);
			xdg_surface_ack_configure(o.xdg_surface, o.configure_serial);
			wl_surface_commit(o.surface);
			ok = settle(&c);
		}
		result(ok ? ST_PASS : ST_FAIL, "attach-after-configure-before-ack",
			ok ? "mapped without a protocol error"
				: "client disconnected or never configured");
		if (ok) {
			xdg_toplevel_destroy(o.xdg_toplevel);
			xdg_surface_destroy(o.xdg_surface);
			wl_surface_destroy(o.surface);
			settle(&c);
		}
	}

	/* xdg-shell: the initial commit of a popup must be answered with
	 * xdg_popup.configure + xdg_surface.configure, or the client waits
	 * forever (menus and tooltips never open). */
	struct popup p;
	popup_create(&c, &t, &p);
	bool ok = settle(&c);
	result(ok && p.configured && p.popup_configured ? ST_PASS : ST_FAIL,
		"popup-initial-configure",
		"xdg_surface.configure %s, xdg_popup.configure %s (%d,%d %dx%d)",
		p.configured ? "received" : "MISSING",
		p.popup_configured ? "received" : "MISSING", p.x, p.y, p.width,
		p.height);

	/* The positioner asks for 20x10 below-right of the anchor rect's
	 * bottom-right corner (14,14); the 64x48 parent leaves room. */
	result(p.popup_configured && p.x == 14 && p.y == 14 && p.width == 20
			&& p.height == 10 ? ST_PASS : ST_FAIL, "popup-positioner-geometry",
		"configured at %d,%d %dx%d (expected 14,14 20x10)", p.x, p.y,
		p.width, p.height);

	/* Map it: ack + buffer. */
	if (p.configured) {
		xdg_surface_ack_configure(p.xdg_surface, p.serial);
		attach_color(&c, p.surface, 20, 10, COLOR_GRAND);
		struct frame_wait wait = {0};
		struct wl_callback *cb = wl_surface_frame(p.surface);
		wl_callback_add_listener(cb, &sFrameListener, &wait);
		wl_surface_commit(p.surface);
		ok = settle(&c);
		wait_for_count(&c, &wait.done, 1);
		result(ok ? ST_PASS : ST_FAIL, "popup-map",
			ok ? "mapped without a protocol error" : "client disconnected");
		/* A client throttled on its popup's frame callback must not
		 * stall (same rule as toplevels, koan_server.c). */
		result(ok && wait.done == 1 ? ST_PASS : ST_FAIL,
			"popup-frame-callback", "%d of 1 frame callbacks done",
			wait.done);
	} else {
		result(ST_BLOCKED, "popup-map", "popup was never configured");
		result(ST_BLOCKED, "popup-frame-callback",
			"popup was never configured");
	}

	/* A mapped popup becomes a native window (create_popup) at its place
	 * in the parent's content coordinates (positioner: 14,14 20x10), and
	 * gets its own pixels once app_server reports the window. Nautilus's
	 * menus were invisible without this, and their pointer grab made the
	 * app look frozen. */
	static int popup_window;
	{
		pthread_mutex_lock(&sPopupLock);
		bool asked = sPopupWm.creates == 1
			&& sPopupWm.toplevel == main_toplevel
			&& sPopupWm.x == 14 && sPopupWm.y == 14
			&& sPopupWm.width == 20 && sPopupWm.height == 10;
		result(asked ? ST_PASS : ST_FAIL, "popup-window-requested",
			"create_popup x%d at %d,%d %dx%d (expected 14,14 20x10)",
			sPopupWm.creates, sPopupWm.x, sPopupWm.y, sPopupWm.width,
			sPopupWm.height);
		union koan_cmd_payload ready = {0};
		ready.popup_ready.popup = sPopupWm.popup;
		ready.popup_ready.window = &popup_window;
		pthread_mutex_unlock(&sPopupLock);
		int frames = atomic_load(&sSetBufferCount);
		ok = koan_queue_push(koan_adapter_queue(), KOAN_CMD_POPUP_READY,
			&ready, NULL) != NULL && settle(&c);
		pthread_mutex_lock(&sFrameLock);
		bool shown = sFrameWidth == 20 && sFrameHeight == 10;
		pthread_mutex_unlock(&sFrameLock);
		ok = ok && shown && atomic_load(&sSetBufferCount) == frames + 1
			&& frame_pixel(0, 0) == COLOR_GRAND;
		result(ok ? ST_PASS : ST_FAIL, "popup-displayed",
			"popup frame %s, first pixel 0x%08x",
			shown ? "delivered 20x10" : "NOT delivered", frame_pixel(0, 0));
	}
	int destroys_before_popup = atomic_load(&sDestroyWindowCount);

	/* Destroying the parent first is a protocol error
	 * (xdg_wm_base.not_the_topmost_popup / defunct_surfaces); destroy
	 * in order and check nothing breaks. */
	xdg_popup_destroy(p.xdg_popup);
	xdg_surface_destroy(p.xdg_surface);
	wl_surface_destroy(p.surface);
	ok = settle(&c);
	result(ok ? ST_PASS : ST_FAIL, "popup-destroy",
		ok ? "destroyed cleanly" : "client disconnected");
	result(atomic_load(&sDestroyWindowCount) == destroys_before_popup + 1
			&& atomic_load(&sDestroyedWindow) == &popup_window
			? ST_PASS : ST_FAIL, "popup-window-destroyed",
		"destroy_window x%d for the popup's window",
		atomic_load(&sDestroyWindowCount) - destroys_before_popup);

	/* xdg_popup.popup_done: when the window manager deactivates the
	 * toplevel (the user clicked elsewhere), its popups are dismissed,
	 * like native menus. */
	{
		struct popup q;
		popup_create(&c, &t, &q);
		ok = settle(&c) && q.configured;
		if (ok) {
			xdg_surface_ack_configure(q.xdg_surface, q.serial);
			attach_color(&c, q.surface, 20, 10, COLOR_GRAND);
			wl_surface_commit(q.surface);
			ok = settle(&c);
		}
		union koan_cmd_payload off = {0};
		off.toplevel_configure.toplevel = main_toplevel;
		off.toplevel_configure.maximized = -1;
		off.toplevel_configure.activated = 0;
		ok = ok && koan_queue_push(koan_adapter_queue(),
			KOAN_CMD_TOPLEVEL_CONFIGURE, &off, NULL) != NULL && settle(&c);
		result(ok && q.done ? ST_PASS : ST_FAIL,
			"popup-done-on-deactivation", "popup_done %s (configured %d, "
			"steps ok %d)", q.done ? "received" : "NOT received",
			q.configured, ok);
		xdg_popup_destroy(q.xdg_popup);
		xdg_surface_destroy(q.xdg_surface);
		wl_surface_destroy(q.surface);
		settle(&c);
	}

	struct conn after;
	bool alive = conn_open(&after, s.socket);
	conn_close(&after);
	result(alive ? ST_PASS : ST_FAIL, "xdg-server-alive",
		alive ? "new client served" : "new client failed");
	conn_close(&c);
	server_stop(&s);
	return summary();
}

/* ---- testctl: koan_test_v1 ------------------------------------------- */

/* A thread stands in for app_server: it drains the app_server-direction
 * queue, records what koan_test_v1 asked for and answers each fence with
 * one input-looper part (pushed before the BEGIN, which the Wayland side
 * must accept). sCtlHold stops it, like a busy app_server. */
static pthread_mutex_t sCtlLock = PTHREAD_MUTEX_INITIALIZER;
static struct {
	int places;
	void *place_toplevel;
	int32_t place_x, place_y;
	int motions;
	double x, y;
	int buttons;
	uint32_t button;
	bool pressed;
	int fences;
} sCtl;
static atomic_bool sCtlHold;
static atomic_bool sCtlStop;

static int
ctl_process(struct koan_command *cmd, void *userdata)
{
	(void)userdata;
	pthread_mutex_lock(&sCtlLock);
	switch (cmd->type) {
		case KOAN_CMD_TEST_PLACE:
			sCtl.places++;
			sCtl.place_toplevel = cmd->payload.test_place.toplevel;
			sCtl.place_x = cmd->payload.test_place.x;
			sCtl.place_y = cmd->payload.test_place.y;
			break;
		case KOAN_CMD_TEST_POINTER:
			if (cmd->payload.test_pointer.button == 0) {
				sCtl.motions++;
				sCtl.x = cmd->payload.test_pointer.x;
				sCtl.y = cmd->payload.test_pointer.y;
			} else {
				sCtl.buttons++;
				sCtl.button = cmd->payload.test_pointer.button;
				sCtl.pressed = cmd->payload.test_pointer.pressed;
			}
			break;
		case KOAN_CMD_TEST_FENCE:
		{
			sCtl.fences++;
			union koan_cmd_payload reply = {0};
			reply.test_fence.serial = cmd->payload.test_fence.serial;
			koan_queue_push(koan_adapter_queue(), KOAN_CMD_TEST_FENCE_PART,
				&reply, NULL);
			reply.test_fence.parts = 1;
			koan_queue_push(koan_adapter_queue(), KOAN_CMD_TEST_FENCE_BEGIN,
				&reply, NULL);
			break;
		}
		default:
			break;
	}
	pthread_mutex_unlock(&sCtlLock);
	return 0;
}

static void *
ctl_thread(void *data)
{
	(void)data;
	while (!atomic_load(&sCtlStop)) {
		if (!atomic_load(&sCtlHold)) {
			koan_queue_drain_direction(koan_adapter_queue(),
				KOAN_QUEUE_TO_APP_SERVER, ctl_process, NULL);
		}
		usleep(1000);
	}
	return NULL;
}

static void
ctl_done(void *data, struct wl_callback *callback, uint32_t serial)
{
	(void)serial;
	(*(int *)data)++;
	wl_callback_destroy(callback);
}

static const struct wl_callback_listener sCtlDoneListener = {
	.done = ctl_done,
};

static int
run_testctl(void)
{
	sTestControl = true;
	struct server s;
	if (!server_start(&s)) {
		result(ST_BLOCKED, "testctl", "server start");
		server_stop(&s);
		return summary();
	}
	pthread_t ctl;
	atomic_store(&sCtlStop, false);
	atomic_store(&sCtlHold, false);
	if (pthread_create(&ctl, NULL, ctl_thread, NULL) != 0) {
		result(ST_BLOCKED, "testctl", "app_server stand-in thread");
		server_stop(&s);
		return summary();
	}

	struct conn c;
	int count = 0;
	const struct global *g = conn_open(&c, s.socket)
		? find_global(&c, "koan_test_v1", &count) : NULL;
	result(g != NULL && count == 1 ? ST_PASS : ST_FAIL,
		"testctl-global-advertised", "koan_test_v1 advertised %d time(s)",
		count);
	struct toplevel t;
	if (g == NULL || c.wm_base == NULL || !map_toplevel(&c, &t, COLOR_ROOT)) {
		result(ST_BLOCKED, "testctl", "no koan_test_v1 or no toplevel");
		goto out;
	}
	struct koan_test_v1 *test = wl_registry_bind(c.registry, g->name,
		&koan_test_v1_interface, 1);

	/* sync answers only after app_server passed the fence. */
	{
		int done = 0;
		atomic_store(&sCtlHold, true);
		struct wl_callback *cb = koan_test_v1_sync(test);
		wl_callback_add_listener(cb, &sCtlDoneListener, &done);
		bool ok = settle(&c);
		usleep(100 * 1000);
		ok = ok && settle(&c);
		int while_held = done;
		atomic_store(&sCtlHold, false);
		ok = ok && wait_for_count(&c, &done, 1);
		result(ok && while_held == 0 && done == 1 ? ST_PASS : ST_FAIL,
			"testctl-sync-waits-for-app-server",
			"done %d time(s) while app_server was busy, %d after", while_held,
			done);
	}

	/* move_surface names the surface origin; the native window shows the
	 * window geometry, so app_server gets the origin plus its offset. */
	{
		xdg_surface_set_window_geometry(t.xdg_surface, 4, 3, 40, 30);
		wl_surface_commit(t.surface);
		bool ok = settle(&c);
		int done = 0;
		koan_test_v1_move_surface(test, t.surface, 100, 50);
		struct wl_callback *cb = koan_test_v1_sync(test);
		wl_callback_add_listener(cb, &sCtlDoneListener, &done);
		ok = ok && wait_for_count(&c, &done, 1);
		pthread_mutex_lock(&sCtlLock);
		ok = ok && sCtl.places == 1
			&& sCtl.place_toplevel == atomic_load(&sCreatedToplevel)
			&& sCtl.place_x == 104 && sCtl.place_y == 53;
		result(ok ? ST_PASS : ST_FAIL, "testctl-move-surface",
			"%d place command(s), content at %d,%d (expected 104,53)",
			sCtl.places, sCtl.place_x, sCtl.place_y);
		pthread_mutex_unlock(&sCtlLock);
	}

	/* Pointer requests reach app_server unchanged, before the sync. */
	{
		int done = 0;
		koan_test_v1_pointer_motion(test, wl_fixed_from_double(12.5),
			wl_fixed_from_double(7.25));
		koan_test_v1_pointer_button(test, 0x110,
			WL_POINTER_BUTTON_STATE_PRESSED);
		struct wl_callback *cb = koan_test_v1_sync(test);
		wl_callback_add_listener(cb, &sCtlDoneListener, &done);
		bool ok = wait_for_count(&c, &done, 1);
		pthread_mutex_lock(&sCtlLock);
		ok = ok && sCtl.motions == 1 && sCtl.x == 12.5 && sCtl.y == 7.25
			&& sCtl.buttons == 1 && sCtl.button == 0x110 && sCtl.pressed;
		result(ok ? ST_PASS : ST_FAIL, "testctl-pointer",
			"motion x%d to %.2f,%.2f; button x%d 0x%x %s", sCtl.motions,
			sCtl.x, sCtl.y, sCtl.buttons, sCtl.button,
			sCtl.pressed ? "pressed" : "released");
		pthread_mutex_unlock(&sCtlLock);
	}

	koan_test_v1_destroy(test);
	settle(&c);
out:
	conn_close(&c);
	atomic_store(&sCtlStop, true);
	pthread_join(ctl, NULL);
	server_stop(&s);
	return summary();
}

int
main(int argc, char **argv)
{
	const char *mode = argc > 1 ? argv[1] : "core";
	char runtime_dir[] = "/tmp/koan-protocol-test-XXXXXX";

	wlr_log_init(WLR_ERROR, NULL);
	if (mkdtemp(runtime_dir) == NULL) {
		perror("mkdtemp");
		return 1;
	}
	setenv("XDG_RUNTIME_DIR", runtime_dir, 1);
	/* Never bind (or steal) a real session's socket. */
	unsetenv("WAYLAND_DISPLAY");

	int status;
	if (strcmp(mode, "core") == 0) {
		status = run_core();
	} else if (strcmp(mode, "lifecycle") == 0) {
		status = run_lifecycle();
	} else if (strcmp(mode, "bad-buffer") == 0) {
		status = run_bad_buffer();
	} else if (strcmp(mode, "subsurface") == 0) {
		status = run_subsurface();
	} else if (strcmp(mode, "compose") == 0) {
		status = run_compose();
	} else if (strcmp(mode, "xdg") == 0) {
		status = run_xdg();
	} else if (strcmp(mode, "data") == 0) {
		status = run_data();
	} else if (strcmp(mode, "input") == 0) {
		status = run_input();
	} else if (strcmp(mode, "testctl") == 0) {
		status = run_testctl();
	} else {
		fprintf(stderr, "usage: %s [core|lifecycle|bad-buffer|subsurface|"
			"compose|xdg|data|input|testctl]\n", argv[0]);
		status = 2;
	}

	rmdir(runtime_dir);
	return status;
}
