/*
 * Copyright © 2012 Intel Corporation
 * Copyright © 2012, 2013, 2014 Collabora, Ltd.
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
 * all under the MIT (Expat) license above. They are rewritten as plain
 * libwayland clients for the Koan server; no Weston harness code is used.
 * The remaining code is Koan's own and is offered under the same terms.
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
 *   [core|lifecycle|bad-buffer|subsurface|compose|xdg]
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

static void
fake_destroy_window(void *userdata, struct koan_queue *queue, void *window)
{
	(void)userdata;
	(void)queue;
	(void)window;
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
	uint32_t format)
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

static struct koan_window_callbacks sCallbacks = {
	.create_window = fake_create_window,
	.destroy_window = fake_destroy_window,
	.set_title = fake_set_title,
	.set_size = fake_set_size,
	.set_buffer = fake_set_buffer,
	.set_visibility = fake_set_visibility,
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
	"wp_viewporter",
	"wp_presentation",
	"wp_fractional_scale_manager_v1",
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
};

static void
xdg_surface_configure(void *data, struct xdg_surface *xdg_surface,
	uint32_t serial)
{
	(void)xdg_surface;
	struct toplevel *t = data;
	t->configure_serial = serial;
	t->configured = true;
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
	t->xdg_surface = xdg_wm_base_get_xdg_surface(c->wm_base, t->surface);
	xdg_surface_add_listener(t->xdg_surface, &sXdgSurfaceListener, t);
	t->xdg_toplevel = xdg_surface_get_toplevel(t->xdg_surface);
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
		for (int r = 0; ok && wait.done <= i && r < 8; r++)
			ok = wl_display_roundtrip(c->display) >= 0;
	}
	int delivered = atomic_load(&sSetBufferCount) - before;
	uint32_t last = ((kFrames - 1) & 1) ? COLOR_ROOT : COLOR_CHILD;
	ok = ok && wait.done == kFrames && delivered == kFrames
		&& frame_pixel(0, 0) == last;
	result(ok ? ST_PASS : ST_FAIL, "frame-callback-loop",
		"%d/%d callbacks, %d frames delivered, last pixel 0x%08x",
		wait.done, kFrames, delivered, frame_pixel(0, 0));
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
	test_frame_loop(&c, &t);
	test_nested_sync(&c, &t);

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

	/* Showing the popup is app_server's job and has no bridge yet:
	 * koan_window_callbacks only knows toplevels. */
	result(ST_NOT_IMPLEMENTED, "popup-displayed",
		"no native window for xdg_popup in the app_server bridge");

	/* Destroying the parent first is a protocol error
	 * (xdg_wm_base.not_the_topmost_popup / defunct_surfaces); destroy
	 * in order and check nothing breaks. */
	xdg_popup_destroy(p.xdg_popup);
	xdg_surface_destroy(p.xdg_surface);
	wl_surface_destroy(p.surface);
	ok = settle(&c);
	result(ok ? ST_PASS : ST_FAIL, "popup-destroy",
		ok ? "destroyed cleanly" : "client disconnected");

	struct conn after;
	bool alive = conn_open(&after, s.socket);
	conn_close(&after);
	result(alive ? ST_PASS : ST_FAIL, "xdg-server-alive",
		alive ? "new client served" : "new client failed");
	conn_close(&c);
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
	} else {
		fprintf(stderr, "usage: %s [core|lifecycle|bad-buffer|subsurface|"
			"compose|xdg]\n", argv[0]);
		status = 2;
	}

	rmdir(runtime_dir);
	return status;
}
