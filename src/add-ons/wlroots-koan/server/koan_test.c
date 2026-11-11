/*
 * SPDX-License-Identifier: MIT
 * Copyright © 2026 carlos20260001
 *
 * koan_test_v1 server side. See koan_test.h.
 */

#define _GNU_SOURCE
#include <stdlib.h>
#include <sys/stat.h>

#include <wayland-server-core.h>
#include <wayland-server-protocol.h>
#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_xdg_shell.h>
#include <wlr/util/log.h>

#include "koan-test-v1-protocol.h"
#include "koan_adapter.h"
#include "koan_test.h"

/* A sync that never completes would hang the test runner: answer it after
 * this long and log, so the run goes on and the log says why. */
#define KOAN_TEST_FENCE_TIMEOUT_MS 3000

struct koan_test_fence {
	struct wl_list link;          /* sTest.fences */
	uint32_t serial;
	struct wl_resource *callback; /* NULL once the client destroyed it */
	int32_t expected;             /* -1 until FENCE_BEGIN */
	int32_t got;
	struct wl_event_source *timer;
};

static struct {
	struct wl_display *display;
	struct wl_global *global;
	struct wl_list fences;
	uint32_t next_serial;
} sTest;

bool koan_test_control_requested(void) {
	struct stat st;
	return lstat(KOAN_TEST_CONTROL_FILE, &st) == 0 && S_ISREG(st.st_mode)
		&& st.st_uid == 0;
}

static void push(enum koan_cmd_type type, const union koan_cmd_payload *p) {
	struct koan_queue *queue = koan_adapter_queue();
	if (queue == NULL || koan_queue_push(queue, type, p, NULL) == NULL) {
		wlr_log(WLR_ERROR, "koan-test: could not queue command %d", type);
		return;
	}
	koan_adapter_wake_app_server();
}

/* ---- fences ------------------------------------------------------------ */

static void fence_free(struct koan_test_fence *f) {
	if (f->callback != NULL)
		wl_resource_set_user_data(f->callback, NULL);
	if (f->timer != NULL)
		wl_event_source_remove(f->timer);
	wl_list_remove(&f->link);
	free(f);
}

static void fence_finish(struct koan_test_fence *f) {
	if (f->callback != NULL) {
		wl_callback_send_done(f->callback, f->serial);
		wl_resource_destroy(f->callback); /* clears f->callback */
	}
	fence_free(f);
}

static void fence_check(struct koan_test_fence *f) {
	if (f->expected >= 0 && f->got >= f->expected)
		fence_finish(f);
}

static int fence_timeout(void *data) {
	struct koan_test_fence *f = data;
	wlr_log(WLR_ERROR, "koan-test: sync %u timed out (%d of %d input "
		"loopers answered); answering anyway", f->serial, f->got,
		f->expected);
	/* Removing a source from its own callback is allowed (libwayland
	 * defers freeing it). */
	fence_finish(f);
	return 0;
}

static void callback_resource_destroy(struct wl_resource *resource) {
	struct koan_test_fence *f = wl_resource_get_user_data(resource);
	if (f != NULL)
		f->callback = NULL;
}

static struct koan_test_fence *find_fence(uint32_t serial) {
	struct koan_test_fence *f;
	wl_list_for_each(f, &sTest.fences, link) {
		if (f->serial == serial)
			return f;
	}
	return NULL;
}

bool koan_test_handle_command(const struct koan_command *cmd) {
	if (cmd->type != KOAN_CMD_TEST_FENCE_BEGIN
			&& cmd->type != KOAN_CMD_TEST_FENCE_PART)
		return false;
	if (sTest.global == NULL)
		return true;
	struct koan_test_fence *f = find_fence(cmd->payload.test_fence.serial);
	if (f == NULL)
		return true; /* timed out already */
	/* The parts come from the input loopers' threads and may be queued
	 * before app_server's BEGIN; count both ways. */
	if (cmd->type == KOAN_CMD_TEST_FENCE_BEGIN)
		f->expected = cmd->payload.test_fence.parts;
	else
		f->got++;
	fence_check(f);
	return true;
}

/* ---- requests ---------------------------------------------------------- */

static void handle_destroy(struct wl_client *client,
		struct wl_resource *resource) {
	(void)client;
	wl_resource_destroy(resource);
}

static void handle_move_surface(struct wl_client *client,
		struct wl_resource *resource, struct wl_resource *surface_resource,
		int32_t x, int32_t y) {
	(void)client;
	(void)resource;
	struct wlr_surface *surface = wlr_surface_from_resource(surface_resource);
	struct wlr_surface *root = wlr_surface_get_root_surface(surface);
	struct wlr_xdg_toplevel *toplevel =
		wlr_xdg_toplevel_try_from_wlr_surface(root);
	if (toplevel == NULL || !root->mapped) {
		wlr_log(WLR_ERROR, "koan-test: move_surface on a surface without "
			"a mapped toplevel");
		return;
	}
	/* The native window shows koan_adapter_window_box() of the tree,
	 * whose origin is in root-surface coordinates. */
	struct wlr_box box = koan_adapter_window_box(toplevel);
	union koan_cmd_payload p = {0};
	p.test_place.toplevel = toplevel;
	p.test_place.x = x + box.x;
	p.test_place.y = y + box.y;
	push(KOAN_CMD_TEST_PLACE, &p);
}

static void handle_pointer_motion(struct wl_client *client,
		struct wl_resource *resource, wl_fixed_t x, wl_fixed_t y) {
	(void)client;
	(void)resource;
	union koan_cmd_payload p = {0};
	p.test_pointer.x = wl_fixed_to_double(x);
	p.test_pointer.y = wl_fixed_to_double(y);
	push(KOAN_CMD_TEST_POINTER, &p);
}

static void handle_pointer_button(struct wl_client *client,
		struct wl_resource *resource, uint32_t button, uint32_t state) {
	(void)client;
	(void)resource;
	if (button == 0)
		return;
	union koan_cmd_payload p = {0};
	p.test_pointer.button = button;
	p.test_pointer.pressed = state == WL_POINTER_BUTTON_STATE_PRESSED;
	push(KOAN_CMD_TEST_POINTER, &p);
}

static void handle_sync(struct wl_client *client,
		struct wl_resource *resource, uint32_t id) {
	struct wl_resource *callback = wl_resource_create(client,
		&wl_callback_interface, 1, id);
	struct koan_test_fence *f = calloc(1, sizeof(*f));
	if (callback == NULL || f == NULL) {
		free(f);
		if (callback != NULL)
			wl_resource_destroy(callback);
		wl_resource_post_no_memory(resource);
		return;
	}
	f->serial = ++sTest.next_serial;
	f->callback = callback;
	f->expected = -1;
	wl_resource_set_implementation(callback, NULL, f,
		callback_resource_destroy);
	f->timer = wl_event_loop_add_timer(
		wl_display_get_event_loop(sTest.display), fence_timeout, f);
	if (f->timer != NULL)
		wl_event_source_timer_update(f->timer, KOAN_TEST_FENCE_TIMEOUT_MS);
	wl_list_insert(sTest.fences.prev, &f->link);

	union koan_cmd_payload p = {0};
	p.test_fence.serial = f->serial;
	push(KOAN_CMD_TEST_FENCE, &p);
}

static const struct koan_test_v1_interface sImpl = {
	.destroy = handle_destroy,
	.move_surface = handle_move_surface,
	.pointer_motion = handle_pointer_motion,
	.pointer_button = handle_pointer_button,
	.sync = handle_sync,
};

static void bind_test(struct wl_client *client, void *data, uint32_t version,
		uint32_t id) {
	(void)data;
	struct wl_resource *resource = wl_resource_create(client,
		&koan_test_v1_interface, (int)version, id);
	if (resource == NULL) {
		wl_client_post_no_memory(client);
		return;
	}
	wl_resource_set_implementation(resource, &sImpl, NULL, NULL);
}

bool koan_test_control_create(struct wl_display *display) {
	if (sTest.global != NULL)
		return true;
	wl_list_init(&sTest.fences);
	sTest.display = display;
	sTest.global = wl_global_create(display, &koan_test_v1_interface, 1,
		NULL, bind_test);
	if (sTest.global == NULL)
		return false;
	wlr_log(WLR_ERROR, "Koan: koan_test_v1 test control is ENABLED (%s): "
		"clients can move windows and inject input", KOAN_TEST_CONTROL_FILE);
	return true;
}

void koan_test_control_destroy(void) {
	if (sTest.global == NULL)
		return;
	while (!wl_list_empty(&sTest.fences)) {
		struct koan_test_fence *f =
			wl_container_of(sTest.fences.next, f, link);
		fence_free(f);
	}
	wl_global_destroy(sTest.global);
	sTest.global = NULL;
	sTest.display = NULL;
}
