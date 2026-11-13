/*
 * SPDX-License-Identifier: MIT
 * Copyright © 2026 carlos20260001
 */

/*
 * Regression test: a real xdg-shell client must be able to show a window
 * through the Koan Wayland server.
 *
 * The Koan server runs on its own thread, as in app_server. The test plays
 * the app_server role through fake koan_window_callbacks and the
 * KOAN_CMD_WINDOW_READY reply, and drives a minimal libwayland-client
 * toplevel through the XDG shell lifecycle:
 *
 *   1. initial commit without a buffer -> compositor must send configure;
 *   2. ack_configure + commit with an shm buffer -> surface maps and the
 *      adapter asks app_server for a native window;
 *   3. app_server replies WINDOW_READY -> only the pixels inside the
 *      xdg window geometry (no client shadow) reach set_buffer;
 *   4. the frame callback is completed so the client keeps drawing;
 *   5. a subsurface (where Firefox draws its whole window) is composed
 *      over the root, both through the parent's commit (synchronized) and
 *      on its own commit (desynchronized), and gets its frame callback;
 *   5b. input from app_server reaches the right surface: pointer enter at
 *      the local position (subsurface or root), button, wheel, keyboard
 *      focus and keys; input for a stale toplevel is ignored;
 *   6. app_server's window actions reach the client: a configure with
 *      size and maximized/activated states, and close; an action for a
 *      toplevel that no longer exists is ignored;
 *   7. a decoration created before the first commit ends up server-side
 *      (setting it too early used to be an assertion in wlroots);
 *   8. destroying a mapped toplevel and an unmapped one must not crash.
 *
 * Exits 0 on success, 1 on a reported failure; a crash or assertion also
 * fails.
 */

#define _GNU_SOURCE
#include <pthread.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <linux/input-event-codes.h>
#include <unistd.h>

#include <wayland-client.h>
#include <wayland-server-core.h>
#include <wlr/util/log.h>

#include "xdg-shell-client-protocol.h"
#include "xdg-decoration-unstable-v1-client-protocol.h"

#include "koan_adapter.h"
#include "koan_frame.h"
#include "koan_queue.h"
#include "koan_server.h"

#define TEST_WIDTH 64
#define TEST_HEIGHT 48
/* xdg window geometry inside the buffer; the rest is client "shadow". */
#define GEOM_X 8
#define GEOM_Y 6
#define GEOM_W 48
#define GEOM_H 36
#define SHADOW_PIXEL 0x00000000u
#define CONTENT_PIXEL 0xff336699u
/* A subsurface covering the left half of the window geometry. */
#define SUB_W (GEOM_W / 2)
#define SUB_PIXEL_1 0xffcc3300u
#define SUB_PIXEL_2 0xff00cc33u

/* ---- fake app_server (runs on the Wayland thread) ---------------------- */

static atomic_int sCreateCount;
static atomic_int sSetBufferCount;
static atomic_int sDestroyCount;
static atomic_int sBadBufferCount; /* wrong size or shadow pixels */
static atomic_uint sLastSubPixel;  /* subsurface colour of the last frame */
static _Atomic(struct wlr_xdg_toplevel *) sCreatedToplevel;
static int sNativeWindowToken; /* address used as the native handle */

static void
fake_create_window(void *userdata, struct koan_queue *queue,
	struct wlr_xdg_toplevel *toplevel, int32_t width, int32_t height)
{
	(void)userdata;
	(void)queue;
	(void)width;
	(void)height;
	atomic_store(&sCreatedToplevel, toplevel);
	atomic_fetch_add(&sCreateCount, 1);
}

static void
fake_destroy_window(void *userdata, struct koan_queue *queue, void *window)
{
	(void)userdata;
	(void)queue;
	(void)window;
	atomic_fetch_add(&sDestroyCount, 1);
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
	uint32_t format, float scale, int32_t acked_width, int32_t acked_height)
{
	(void)scale; /* 1: these tests never set an output scale */
	(void)acked_width;
	(void)acked_height;
	(void)userdata;
	(void)queue;
	(void)window;
	(void)format;
	/* Only the window geometry may reach app_server: cropped size, no
	 * shadow pixel; the left half is the subsurface, the right half the
	 * root's content. */
	bool ok = width == GEOM_W && height == GEOM_H && stride >= GEOM_W * 4;
	uint32_t sub = ok ? *(const uint32_t *)data : 0;
	ok = ok && (sub == SUB_PIXEL_1 || sub == SUB_PIXEL_2);
	for (int32_t y = 0; ok && y < height; y++) {
		const uint32_t *row = (const uint32_t *)((const uint8_t *)data
			+ (size_t)y * stride);
		for (int32_t x = 0; x < width; x++) {
			if (row[x] != (x < SUB_W ? sub : CONTENT_PIXEL)) {
				ok = false;
				break;
			}
		}
	}
	if (ok)
		atomic_store(&sLastSubPixel, sub);
	atomic_fetch_add(ok ? &sSetBufferCount : &sBadBufferCount, 1);
	koan_frame_release(data); /* set_buffer takes ownership */
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

static void *
server_thread(void *arg)
{
	koan_server_run((struct wl_display *)arg);
	return NULL;
}

/* ---- client ------------------------------------------------------------ */

struct client {
	struct wl_display *display;
	struct wl_compositor *compositor;
	struct wl_shm *shm;
	struct wl_subcompositor *subcompositor;
	struct wl_seat *seat;
	struct xdg_wm_base *wm_base;
	struct zxdg_decoration_manager_v1 *decoration_manager;
};

struct toplevel {
	struct wl_surface *surface;
	struct xdg_surface *xdg_surface;
	struct xdg_toplevel *xdg_toplevel;
	uint32_t configure_serial;
	bool configured;
	bool frame_done;
	int32_t width, height;      /* last xdg_toplevel.configure */
	bool maximized, activated;
	bool closed;
	uint32_t decoration_mode;   /* 0 until configured */
};

static const struct wl_seat_listener sSeatListener;

static void
registry_global(void *data, struct wl_registry *registry, uint32_t name,
	const char *interface, uint32_t version)
{
	struct client *c = data;
	(void)version;
	if (strcmp(interface, wl_compositor_interface.name) == 0
			&& c->compositor == NULL)
		c->compositor = wl_registry_bind(registry, name,
			&wl_compositor_interface, 4);
	else if (strcmp(interface, wl_shm_interface.name) == 0 && c->shm == NULL)
		c->shm = wl_registry_bind(registry, name, &wl_shm_interface, 1);
	else if (strcmp(interface, wl_seat_interface.name) == 0
			&& c->seat == NULL) {
		/* Listen right away: capabilities is sent on bind. */
		c->seat = wl_registry_bind(registry, name, &wl_seat_interface, 5);
		wl_seat_add_listener(c->seat, &sSeatListener, NULL);
	}
	else if (strcmp(interface, wl_subcompositor_interface.name) == 0
			&& c->subcompositor == NULL)
		c->subcompositor = wl_registry_bind(registry, name,
			&wl_subcompositor_interface, 1);
	else if (strcmp(interface, xdg_wm_base_interface.name) == 0
			&& c->wm_base == NULL)
		c->wm_base = wl_registry_bind(registry, name,
			&xdg_wm_base_interface, 1);
	else if (strcmp(interface, zxdg_decoration_manager_v1_interface.name) == 0
			&& c->decoration_manager == NULL)
		c->decoration_manager = wl_registry_bind(registry, name,
			&zxdg_decoration_manager_v1_interface, 1);
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
wm_base_ping(void *data, struct xdg_wm_base *wm_base, uint32_t serial)
{
	(void)data;
	xdg_wm_base_pong(wm_base, serial);
}

static const struct xdg_wm_base_listener sWmBaseListener = {
	.ping = wm_base_ping,
};

static void
xdg_surface_configure(void *data, struct xdg_surface *xdg_surface,
	uint32_t serial)
{
	struct toplevel *t = data;
	(void)xdg_surface;
	t->configure_serial = serial;
	t->configured = true;
}

static const struct xdg_surface_listener sXdgSurfaceListener = {
	.configure = xdg_surface_configure,
};

static void
toplevel_configure(void *data, struct xdg_toplevel *xdg_toplevel,
	int32_t width, int32_t height, struct wl_array *states)
{
	struct toplevel *t = data;
	(void)xdg_toplevel;
	t->width = width;
	t->height = height;
	t->maximized = false;
	t->activated = false;
	uint32_t *state;
	wl_array_for_each(state, states) {
		if (*state == XDG_TOPLEVEL_STATE_MAXIMIZED)
			t->maximized = true;
		else if (*state == XDG_TOPLEVEL_STATE_ACTIVATED)
			t->activated = true;
	}
}

static void
toplevel_close(void *data, struct xdg_toplevel *xdg_toplevel)
{
	struct toplevel *t = data;
	(void)xdg_toplevel;
	t->closed = true;
}

static void
decoration_configure(void *data,
	struct zxdg_toplevel_decoration_v1 *decoration, uint32_t mode)
{
	struct toplevel *t = data;
	(void)decoration;
	t->decoration_mode = mode;
}

static const struct zxdg_toplevel_decoration_v1_listener sDecorationListener = {
	.configure = decoration_configure,
};

/* What the client's seat saw. */
static struct {
	uint32_t caps;
	struct wl_surface *pointer_surface;
	double px, py;            /* last enter/motion position */
	uint32_t last_button;
	uint32_t button_state;
	int buttons;
	double axis;
	bool keymap;
	struct wl_surface *keyboard_surface;
	uint32_t last_key;
	uint32_t key_state;
	int keys;
} sSeat;

static void seat_capabilities(void *data, struct wl_seat *seat, uint32_t caps)
{
	(void)data;
	(void)seat;
	sSeat.caps = caps;
}

static void seat_name(void *data, struct wl_seat *seat, const char *name)
{
	(void)data;
	(void)seat;
	(void)name;
}

static const struct wl_seat_listener sSeatListener = {
	.capabilities = seat_capabilities,
	.name = seat_name,
};

static void pointer_enter(void *data, struct wl_pointer *pointer,
	uint32_t serial, struct wl_surface *surface, wl_fixed_t x, wl_fixed_t y)
{
	(void)data; (void)pointer; (void)serial;
	sSeat.pointer_surface = surface;
	sSeat.px = wl_fixed_to_double(x);
	sSeat.py = wl_fixed_to_double(y);
}

static void pointer_leave(void *data, struct wl_pointer *pointer,
	uint32_t serial, struct wl_surface *surface)
{
	(void)data; (void)pointer; (void)serial; (void)surface;
	sSeat.pointer_surface = NULL;
}

static void pointer_motion(void *data, struct wl_pointer *pointer,
	uint32_t time, wl_fixed_t x, wl_fixed_t y)
{
	(void)data; (void)pointer; (void)time;
	sSeat.px = wl_fixed_to_double(x);
	sSeat.py = wl_fixed_to_double(y);
}

static void pointer_button(void *data, struct wl_pointer *pointer,
	uint32_t serial, uint32_t time, uint32_t button, uint32_t state)
{
	(void)data; (void)pointer; (void)serial; (void)time;
	sSeat.last_button = button;
	sSeat.button_state = state;
	sSeat.buttons++;
}

static void pointer_axis(void *data, struct wl_pointer *pointer,
	uint32_t time, uint32_t axis, wl_fixed_t value)
{
	(void)data; (void)pointer; (void)time;
	if (axis == WL_POINTER_AXIS_VERTICAL_SCROLL)
		sSeat.axis += wl_fixed_to_double(value);
}

static void pointer_frame(void *data, struct wl_pointer *pointer)
{
	(void)data; (void)pointer;
}

static void pointer_axis_source(void *data, struct wl_pointer *pointer,
	uint32_t source)
{
	(void)data; (void)pointer; (void)source;
}

static void pointer_axis_stop(void *data, struct wl_pointer *pointer,
	uint32_t time, uint32_t axis)
{
	(void)data; (void)pointer; (void)time; (void)axis;
}

static void pointer_axis_discrete(void *data, struct wl_pointer *pointer,
	uint32_t axis, int32_t discrete)
{
	(void)data; (void)pointer; (void)axis; (void)discrete;
}

static const struct wl_pointer_listener sPointerListener = {
	.enter = pointer_enter,
	.leave = pointer_leave,
	.motion = pointer_motion,
	.button = pointer_button,
	.axis = pointer_axis,
	.frame = pointer_frame,
	.axis_source = pointer_axis_source,
	.axis_stop = pointer_axis_stop,
	.axis_discrete = pointer_axis_discrete,
};

static void keyboard_keymap(void *data, struct wl_keyboard *keyboard,
	uint32_t format, int32_t fd, uint32_t size)
{
	(void)data; (void)keyboard; (void)size;
	sSeat.keymap = format == WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1;
	close(fd);
}

static void keyboard_enter(void *data, struct wl_keyboard *keyboard,
	uint32_t serial, struct wl_surface *surface, struct wl_array *keys)
{
	(void)data; (void)keyboard; (void)serial; (void)keys;
	sSeat.keyboard_surface = surface;
}

static void keyboard_leave(void *data, struct wl_keyboard *keyboard,
	uint32_t serial, struct wl_surface *surface)
{
	(void)data; (void)keyboard; (void)serial; (void)surface;
	sSeat.keyboard_surface = NULL;
}

static void keyboard_key(void *data, struct wl_keyboard *keyboard,
	uint32_t serial, uint32_t time, uint32_t key, uint32_t state)
{
	(void)data; (void)keyboard; (void)serial; (void)time;
	sSeat.last_key = key;
	sSeat.key_state = state;
	sSeat.keys++;
}

static void keyboard_modifiers(void *data, struct wl_keyboard *keyboard,
	uint32_t serial, uint32_t depressed, uint32_t latched, uint32_t locked,
	uint32_t group)
{
	(void)data; (void)keyboard; (void)serial; (void)depressed;
	(void)latched; (void)locked; (void)group;
}

static void keyboard_repeat_info(void *data, struct wl_keyboard *keyboard,
	int32_t rate, int32_t delay)
{
	(void)data; (void)keyboard; (void)rate; (void)delay;
}

static const struct wl_keyboard_listener sKeyboardListener = {
	.keymap = keyboard_keymap,
	.enter = keyboard_enter,
	.leave = keyboard_leave,
	.key = keyboard_key,
	.modifiers = keyboard_modifiers,
	.repeat_info = keyboard_repeat_info,
};

static bool
push(enum koan_cmd_type type, union koan_cmd_payload *payload)
{
	return koan_queue_push(koan_adapter_queue(), type, payload, NULL) != NULL;
}

static const struct xdg_toplevel_listener sToplevelListener = {
	.configure = toplevel_configure,
	.close = toplevel_close,
};

static void
frame_done(void *data, struct wl_callback *callback, uint32_t time)
{
	bool *done = data;
	(void)time;
	*done = true;
	wl_callback_destroy(callback);
}

static const struct wl_callback_listener sFrameListener = {
	.done = frame_done,
};

static struct zxdg_toplevel_decoration_v1 *
toplevel_create(struct client *c, struct toplevel *t, const char *title,
	bool decorate)
{
	struct zxdg_toplevel_decoration_v1 *decoration = NULL;
	memset(t, 0, sizeof(*t));
	t->surface = wl_compositor_create_surface(c->compositor);
	t->xdg_surface = xdg_wm_base_get_xdg_surface(c->wm_base, t->surface);
	xdg_surface_add_listener(t->xdg_surface, &sXdgSurfaceListener, t);
	t->xdg_toplevel = xdg_surface_get_toplevel(t->xdg_surface);
	xdg_toplevel_add_listener(t->xdg_toplevel, &sToplevelListener, t);
	xdg_toplevel_set_title(t->xdg_toplevel, title);
	if (decorate) {
		/* Like GTK/Qt: create the decoration before the first commit and
		 * ask to draw the frame ourselves. */
		decoration = zxdg_decoration_manager_v1_get_toplevel_decoration(
			c->decoration_manager, t->xdg_toplevel);
		zxdg_toplevel_decoration_v1_add_listener(decoration,
			&sDecorationListener, t);
		zxdg_toplevel_decoration_v1_set_mode(decoration,
			ZXDG_TOPLEVEL_DECORATION_V1_MODE_CLIENT_SIDE);
	}
	/* Initial commit: no buffer, asks the compositor for a configure. */
	wl_surface_commit(t->surface);
	return decoration;
}

static void
toplevel_destroy(struct toplevel *t)
{
	xdg_toplevel_destroy(t->xdg_toplevel);
	xdg_surface_destroy(t->xdg_surface);
	wl_surface_destroy(t->surface);
}

/* solid == 0: the root pattern (content inside the geometry, shadow
 * outside); otherwise a buffer filled with that colour. */
static struct wl_buffer *
create_shm_buffer(struct client *c, int width, int height, uint32_t solid)
{
	int stride = width * 4;
	int size = stride * height;
	int fd = memfd_create("koan-test", MFD_CLOEXEC);
	if (fd < 0 || ftruncate(fd, size) < 0)
		return NULL;
	void *pixels = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED,
		fd, 0);
	if (pixels == MAP_FAILED) {
		close(fd);
		return NULL;
	}
	for (int y = 0; y < height; y++) {
		uint32_t *row = (uint32_t *)((uint8_t *)pixels + y * stride);
		for (int x = 0; x < width; x++) {
			bool inside = x >= GEOM_X && x < GEOM_X + GEOM_W
				&& y >= GEOM_Y && y < GEOM_Y + GEOM_H;
			row[x] = solid ? solid
				: inside ? CONTENT_PIXEL : SHADOW_PIXEL;
		}
	}
	munmap(pixels, size);

	struct wl_shm_pool *pool = wl_shm_create_pool(c->shm, fd, size);
	struct wl_buffer *buffer = wl_shm_pool_create_buffer(pool, 0,
		width, height, stride, WL_SHM_FORMAT_XRGB8888);
	wl_shm_pool_destroy(pool);
	close(fd);
	return buffer;
}

/* Round-trip a few times so the server thread and its queue drain settle. */
static bool
settle(struct client *c)
{
	for (int i = 0; i < 4; i++) {
		if (wl_display_roundtrip(c->display) < 0)
			return false;
	}
	return true;
}

/* Frame callbacks are answered at the output's pace (a timer in
 * koan_server.c), not inside the commit: dispatch for up to about a
 * second until the flag is set. */
static bool
wait_for_flag(struct client *c, const bool *flag)
{
	for (int i = 0; i < 500 && !*flag; i++) {
		if (wl_display_roundtrip(c->display) < 0)
			return false;
		if (!*flag)
			usleep(2000);
	}
	return *flag;
}

#define CHECK(cond, msg) \
	do { \
		if (!(cond)) { \
			fprintf(stderr, "FAIL: %s\n", msg); \
			return 1; \
		} \
	} while (0)

int
main(void)
{
	char runtime_dir[] = "/tmp/koan-xdg-test-XXXXXX";
	static struct koan_window_callbacks cbs = {
		.create_window = fake_create_window,
		.destroy_window = fake_destroy_window,
		.set_title = fake_set_title,
		.set_size = fake_set_size,
		.set_buffer = fake_set_buffer,
		.set_visibility = fake_set_visibility,
	};

	wlr_log_init(WLR_ERROR, NULL);

	if (mkdtemp(runtime_dir) == NULL) {
		perror("mkdtemp");
		return 1;
	}
	setenv("XDG_RUNTIME_DIR", runtime_dir, 1);

	/* Same order as app_server: callbacks, create, socket, start, run. */
	koan_adapter_set_window_callbacks(&cbs, NULL, &sNativeWindowToken);
	struct wl_display *server = koan_server_create();
	CHECK(server != NULL, "koan_server_create() returned NULL");
	const char *socket = koan_server_add_socket(server);
	CHECK(socket != NULL, "koan_server_add_socket() returned NULL");
	CHECK(koan_server_start(), "koan_server_start() returned false");

	pthread_t thread;
	CHECK(pthread_create(&thread, NULL, server_thread, server) == 0,
		"pthread_create() failed");

	struct client c = {0};
	c.display = wl_display_connect(socket);
	CHECK(c.display != NULL, "wl_display_connect() failed");
	struct wl_registry *registry = wl_display_get_registry(c.display);
	wl_registry_add_listener(registry, &sRegistryListener, &c);
	CHECK(settle(&c), "registry roundtrip failed");
	CHECK(c.compositor && c.shm && c.subcompositor && c.wm_base
		&& c.decoration_manager,
		"missing wl_compositor, wl_shm, wl_subcompositor, xdg_wm_base or "
		"zxdg_decoration_manager_v1 global");
	xdg_wm_base_add_listener(c.wm_base, &sWmBaseListener, NULL);
	CHECK(c.seat != NULL, "missing wl_seat global");
	CHECK((sSeat.caps & WL_SEAT_CAPABILITY_POINTER)
		&& (sSeat.caps & WL_SEAT_CAPABILITY_KEYBOARD),
		"the seat does not offer both pointer and keyboard");
	struct wl_pointer *pointer = wl_seat_get_pointer(c.seat);
	wl_pointer_add_listener(pointer, &sPointerListener, NULL);
	struct wl_keyboard *keyboard = wl_seat_get_keyboard(c.seat);
	wl_keyboard_add_listener(keyboard, &sKeyboardListener, NULL);
	CHECK(settle(&c), "pointer/keyboard roundtrip failed");
	CHECK(sSeat.keymap, "the keyboard sent no xkb keymap");

	/* 1. Initial commit must be answered with a configure. */
	struct toplevel mapped;
	struct zxdg_toplevel_decoration_v1 *decoration =
		toplevel_create(&c, &mapped, "mapped", true);
	CHECK(settle(&c), "roundtrip after initial commit failed");
	CHECK(mapped.configured,
		"no xdg_surface.configure after the initial commit; clients "
		"wait forever and never map");
	/* 7. (checked early: it is part of the first configure) */
	CHECK(mapped.decoration_mode
		== ZXDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE,
		"a decoration created before the first commit was not switched to "
		"server-side, so the client draws its own frame");

	/* 2. Ack + buffer commit maps the surface. A synchronized subsurface
	 * committed first is applied with the parent's commit. */
	xdg_surface_ack_configure(mapped.xdg_surface, mapped.configure_serial);
	xdg_surface_set_window_geometry(mapped.xdg_surface, GEOM_X, GEOM_Y,
		GEOM_W, GEOM_H);
	struct wl_surface *child = wl_compositor_create_surface(c.compositor);
	struct wl_subsurface *subsurface = wl_subcompositor_get_subsurface(
		c.subcompositor, child, mapped.surface);
	wl_subsurface_set_position(subsurface, GEOM_X, GEOM_Y);
	struct wl_buffer *sub_buffer_1 = create_shm_buffer(&c, SUB_W, GEOM_H,
		SUB_PIXEL_1);
	struct wl_buffer *sub_buffer_2 = create_shm_buffer(&c, SUB_W, GEOM_H,
		SUB_PIXEL_2);
	CHECK(sub_buffer_1 && sub_buffer_2, "failed to create shm buffers");
	wl_surface_attach(child, sub_buffer_1, 0, 0);
	wl_surface_damage(child, 0, 0, SUB_W, GEOM_H);
	wl_surface_commit(child);
	struct wl_buffer *buffer = create_shm_buffer(&c, TEST_WIDTH, TEST_HEIGHT,
		0);
	CHECK(buffer != NULL, "failed to create shm buffer");
	wl_surface_attach(mapped.surface, buffer, 0, 0);
	wl_surface_damage(mapped.surface, 0, 0, TEST_WIDTH, TEST_HEIGHT);
	struct wl_callback *frame = wl_surface_frame(mapped.surface);
	wl_callback_add_listener(frame, &sFrameListener, &mapped.frame_done);
	wl_surface_commit(mapped.surface);
	CHECK(settle(&c), "roundtrip after buffer commit failed");
	CHECK(atomic_load(&sCreateCount) == 1,
		"mapped toplevel did not request exactly one native window");

	/* 3. app_server replies with the native window; pixels follow. */
	union koan_cmd_payload ready = {0};
	ready.window_ready.toplevel = atomic_load(&sCreatedToplevel);
	ready.window_ready.window = &sNativeWindowToken;
	CHECK(koan_queue_push(koan_adapter_queue(), KOAN_CMD_WINDOW_READY,
		&ready, NULL) != NULL, "failed to queue WINDOW_READY");
	CHECK(settle(&c), "roundtrip after WINDOW_READY failed");
	CHECK(atomic_load(&sBadBufferCount) == 0,
		"set_buffer did not get exactly the xdg window geometry with the "
		"subsurface composed over the root (whole buffer, shadow pixels or "
		"a missing subsurface)");
	CHECK(atomic_load(&sSetBufferCount) >= 1,
		"committed pixels never reached set_buffer");
	CHECK(atomic_load(&sLastSubPixel) == SUB_PIXEL_1,
		"the synchronized subsurface was not composed");

	/* 4. The frame callback must complete, or clients stop drawing. */
	CHECK(wait_for_flag(&c, &mapped.frame_done),
		"wl_surface.frame callback never completed; clients freeze "
		"after their first frame");

	/* 5. A desynchronized subsurface commits on its own: its new pixels
	 * must reach app_server without a parent commit, and its own frame
	 * callback must complete. */
	wl_subsurface_set_desync(subsurface);
	bool child_frame_done = false;
	wl_surface_attach(child, sub_buffer_2, 0, 0);
	wl_surface_damage(child, 0, 0, SUB_W, GEOM_H);
	frame = wl_surface_frame(child);
	wl_callback_add_listener(frame, &sFrameListener, &child_frame_done);
	wl_surface_commit(child);
	CHECK(settle(&c), "roundtrip after subsurface commit failed");
	CHECK(atomic_load(&sBadBufferCount) == 0,
		"a frame after the subsurface commit was composed wrongly");
	CHECK(atomic_load(&sLastSubPixel) == SUB_PIXEL_2,
		"a desynchronized subsurface commit never reached set_buffer");
	CHECK(wait_for_flag(&c, &child_frame_done),
		"the subsurface's frame callback never completed; Firefox stops "
		"drawing its content after one frame");

	/* 5b. Input from app_server. Window-content coordinates: the
	 * subsurface covers x < SUB_W at the content origin. */
	struct wlr_xdg_toplevel *key = atomic_load(&sCreatedToplevel);
	union koan_cmd_payload in = {0};
	in.pointer_motion.toplevel = key;
	in.pointer_motion.time = 1;
	in.pointer_motion.x = 5;
	in.pointer_motion.y = 7;
	CHECK(push(KOAN_CMD_POINTER_MOTION, &in), "queue POINTER_MOTION");
	CHECK(settle(&c), "roundtrip after pointer motion failed");
	CHECK(sSeat.pointer_surface == child && sSeat.px == 5 && sSeat.py == 7,
		"pointer did not enter the subsurface at its local position");

	memset(&in, 0, sizeof(in));
	in.pointer_motion.toplevel = key;
	in.pointer_motion.time = 2;
	in.pointer_motion.x = SUB_W + 3;
	in.pointer_motion.y = 7;
	CHECK(push(KOAN_CMD_POINTER_MOTION, &in), "queue POINTER_MOTION");
	CHECK(settle(&c), "roundtrip after pointer motion failed");
	CHECK(sSeat.pointer_surface == mapped.surface
		&& sSeat.px == GEOM_X + SUB_W + 3 && sSeat.py == GEOM_Y + 7,
		"pointer did not move onto the root surface in root coordinates "
		"(window geometry offset)");

	memset(&in, 0, sizeof(in));
	in.pointer_button.toplevel = key;
	in.pointer_button.time = 3;
	in.pointer_button.button = BTN_LEFT;
	in.pointer_button.pressed = 1;
	CHECK(push(KOAN_CMD_POINTER_BUTTON, &in), "queue POINTER_BUTTON");
	in.pointer_button.pressed = 0;
	CHECK(push(KOAN_CMD_POINTER_BUTTON, &in), "queue POINTER_BUTTON");
	memset(&in, 0, sizeof(in));
	in.pointer_axis.toplevel = key;
	in.pointer_axis.time = 4;
	in.pointer_axis.steps = 2;
	CHECK(push(KOAN_CMD_POINTER_AXIS, &in), "queue POINTER_AXIS");
	CHECK(settle(&c), "roundtrip after buttons failed");
	CHECK(sSeat.buttons == 2 && sSeat.last_button == BTN_LEFT
		&& sSeat.button_state == WL_POINTER_BUTTON_STATE_RELEASED,
		"left click did not reach the client as press + release");
	CHECK(sSeat.axis == 30.0, "two wheel clicks did not scroll by 30");

	memset(&in, 0, sizeof(in));
	in.keyboard_focus.toplevel = key;
	in.keyboard_focus.focused = 1;
	CHECK(push(KOAN_CMD_KEYBOARD_FOCUS, &in), "queue KEYBOARD_FOCUS");
	memset(&in, 0, sizeof(in));
	in.keyboard_key.toplevel = key;
	in.keyboard_key.time = 5;
	in.keyboard_key.key = KEY_A;
	in.keyboard_key.pressed = 1;
	CHECK(push(KOAN_CMD_KEYBOARD_KEY, &in), "queue KEYBOARD_KEY");
	in.keyboard_key.pressed = 0;
	CHECK(push(KOAN_CMD_KEYBOARD_KEY, &in), "queue KEYBOARD_KEY");
	CHECK(settle(&c), "roundtrip after keys failed");
	CHECK(sSeat.keyboard_surface == mapped.surface,
		"keyboard focus did not reach the toplevel");
	CHECK(sSeat.keys == 2 && sSeat.last_key == KEY_A
		&& sSeat.key_state == WL_KEYBOARD_KEY_STATE_RELEASED,
		"key A did not reach the client as press + release");

	/* A stale toplevel key must not move anything. */
	memset(&in, 0, sizeof(in));
	in.pointer_motion.toplevel =
		(struct wlr_xdg_toplevel *)&sNativeWindowToken;
	in.pointer_motion.x = 1;
	in.pointer_motion.y = 1;
	CHECK(push(KOAN_CMD_POINTER_MOTION, &in), "queue stale motion");
	CHECK(settle(&c), "server died on input for a stale toplevel");
	CHECK(sSeat.px == GEOM_X + SUB_W + 3, "input for a stale toplevel moved "
		"the pointer");

	/* Leaving the window clears pointer focus. */
	memset(&in, 0, sizeof(in));
	in.pointer_motion.toplevel = key;
	in.pointer_motion.leave = 1;
	CHECK(push(KOAN_CMD_POINTER_MOTION, &in), "queue pointer leave");
	CHECK(settle(&c), "roundtrip after pointer leave failed");
	CHECK(sSeat.pointer_surface == NULL, "pointer leave was not delivered");

	/* 6. Window actions from app_server. */
	union koan_cmd_payload action = {0};
	action.toplevel_configure.toplevel = atomic_load(&sCreatedToplevel);
	action.toplevel_configure.width = 100;
	action.toplevel_configure.height = 80;
	action.toplevel_configure.maximized = 1;
	action.toplevel_configure.activated = 1;
	CHECK(koan_queue_push(koan_adapter_queue(), KOAN_CMD_TOPLEVEL_CONFIGURE,
		&action, NULL) != NULL, "failed to queue TOPLEVEL_CONFIGURE");
	CHECK(settle(&c), "roundtrip after TOPLEVEL_CONFIGURE failed");
	CHECK(mapped.width == 100 && mapped.height == 80 && mapped.maximized
		&& mapped.activated,
		"TOPLEVEL_CONFIGURE did not reach the client as a configure with "
		"size, maximized and activated");

	/* A key that is not (or no longer) a toplevel must be ignored. */
	union koan_cmd_payload stale = {0};
	stale.toplevel_close.toplevel = (struct wlr_xdg_toplevel *)&sNativeWindowToken;
	CHECK(koan_queue_push(koan_adapter_queue(), KOAN_CMD_TOPLEVEL_CLOSE,
		&stale, NULL) != NULL, "failed to queue stale TOPLEVEL_CLOSE");
	CHECK(settle(&c), "server died on a close for a stale toplevel");
	CHECK(!mapped.closed, "a stale close reached the wrong toplevel");

	union koan_cmd_payload close_cmd = {0};
	close_cmd.toplevel_close.toplevel = atomic_load(&sCreatedToplevel);
	CHECK(koan_queue_push(koan_adapter_queue(), KOAN_CMD_TOPLEVEL_CLOSE,
		&close_cmd, NULL) != NULL, "failed to queue TOPLEVEL_CLOSE");
	CHECK(settle(&c), "roundtrip after TOPLEVEL_CLOSE failed");
	CHECK(mapped.closed,
		"TOPLEVEL_CLOSE (decorator close button) never reached the client");

	/* 8. Destroy an unmapped toplevel, then the mapped one. */
	struct toplevel unmapped;
	toplevel_create(&c, &unmapped, "unmapped", false);
	CHECK(settle(&c), "roundtrip for unmapped toplevel failed");
	toplevel_destroy(&unmapped);
	CHECK(settle(&c), "server died destroying an unmapped toplevel");

	zxdg_toplevel_decoration_v1_destroy(decoration);
	wl_subsurface_destroy(subsurface);
	wl_surface_destroy(child);
	toplevel_destroy(&mapped);
	wl_buffer_destroy(buffer);
	wl_buffer_destroy(sub_buffer_1);
	wl_buffer_destroy(sub_buffer_2);
	CHECK(settle(&c), "server died destroying a mapped toplevel");
	CHECK(atomic_load(&sDestroyCount) == 1,
		"destroying the mapped toplevel did not destroy its native window");

	wl_display_disconnect(c.display);

	/* Same teardown as WaylandWindowBridge::Shutdown(). */
	wl_display_terminate(server);
	pthread_join(thread, NULL);
	koan_server_destroy(server);
	rmdir(runtime_dir);

	printf("PASS: xdg-shell client configure/map/buffer/frame/subsurface/"
		"actions/decoration/destroy\n");
	return 0;
}
