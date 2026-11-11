/*
 * SPDX-License-Identifier: MIT
 * Copyright © 2026 carlos20260001
 *
 * Manual check for the debug VM (docker/wlcs/run-wlcs-vm.sh builds it next
 * to koan-wlcs-vm-integration.so): does a popup's native window follow its
 * toplevel when app_server moves the toplevel?
 *
 *   koan-popup-follow-check [SECONDS]
 *
 * Maps a 200x150 blue toplevel and, at 20,20 inside it, a 60x40 green
 * popup without a grab (like a tooltip, so nothing dismisses it). Places
 * the toplevel at 100,100 with koan_test_v1, prints "PLACED", waits
 * SECONDS (default 5), moves it to 500,300, prints "MOVED", waits again
 * and exits. Take a screenshot during each wait: the green box must stay
 * at the toplevel's top-left + 20,20 (120,120 then 520,320). Needs
 * /run/koan-test-control (see koan_test.h).
 */

#define _GNU_SOURCE
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#include <wayland-client.h>

#include "koan-test-v1-client-protocol.h"
#include "xdg-shell-client-protocol.h"

static struct wl_compositor *sCompositor;
static struct wl_shm *sShm;
static struct xdg_wm_base *sWmBase;
static struct koan_test_v1 *sTest;

static void
global(void *data, struct wl_registry *registry, uint32_t name,
	const char *interface, uint32_t version)
{
	(void)data;
	(void)version;
	if (strcmp(interface, wl_compositor_interface.name) == 0)
		sCompositor = wl_registry_bind(registry, name, &wl_compositor_interface, 4);
	else if (strcmp(interface, wl_shm_interface.name) == 0)
		sShm = wl_registry_bind(registry, name, &wl_shm_interface, 1);
	else if (strcmp(interface, xdg_wm_base_interface.name) == 0)
		sWmBase = wl_registry_bind(registry, name, &xdg_wm_base_interface, 1);
	else if (strcmp(interface, koan_test_v1_interface.name) == 0)
		sTest = wl_registry_bind(registry, name, &koan_test_v1_interface, 1);
}

static void
global_remove(void *data, struct wl_registry *registry, uint32_t name)
{
	(void)data;
	(void)registry;
	(void)name;
}

static const struct wl_registry_listener sRegistryListener = {
	global, global_remove,
};

static void
ping(void *data, struct xdg_wm_base *base, uint32_t serial)
{
	(void)data;
	xdg_wm_base_pong(base, serial);
}

static const struct xdg_wm_base_listener sWmBaseListener = { ping };

static void
surface_configure(void *data, struct xdg_surface *xdg_surface, uint32_t serial)
{
	xdg_surface_ack_configure(xdg_surface, serial);
	*(bool *)data = true;
}

static const struct xdg_surface_listener sSurfaceListener = {
	surface_configure,
};

static struct wl_buffer *
make_buffer(int w, int h, uint32_t color)
{
	int stride = w * 4, size = stride * h;
	int fd = memfd_create("koan-check", MFD_CLOEXEC);
	if (fd < 0 || ftruncate(fd, size) != 0)
		exit(1);
	uint32_t *pixels = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED,
		fd, 0);
	for (int i = 0; i < w * h; i++)
		pixels[i] = color;
	munmap(pixels, size);
	struct wl_shm_pool *pool = wl_shm_create_pool(sShm, fd, size);
	struct wl_buffer *buffer = wl_shm_pool_create_buffer(pool, 0, w, h,
		stride, WL_SHM_FORMAT_XRGB8888);
	wl_shm_pool_destroy(pool);
	close(fd);
	return buffer;
}

static void
done(void *data, struct wl_callback *callback, uint32_t serial)
{
	(void)serial;
	*(bool *)data = true;
	wl_callback_destroy(callback);
}

static const struct wl_callback_listener sDoneListener = { done };

static void
test_sync(struct wl_display *display)
{
	bool finished = false;
	struct wl_callback *cb = koan_test_v1_sync(sTest);
	wl_callback_add_listener(cb, &sDoneListener, &finished);
	while (!finished && wl_display_dispatch(display) >= 0)
		;
}

static void
map(struct wl_display *display, struct wl_surface *surface,
	struct xdg_surface *xdg, bool *configured, int w, int h, uint32_t color)
{
	xdg_surface_add_listener(xdg, &sSurfaceListener, configured);
	wl_surface_commit(surface);
	while (!*configured && wl_display_dispatch(display) >= 0)
		;
	wl_surface_attach(surface, make_buffer(w, h, color), 0, 0);
	wl_surface_damage(surface, 0, 0, w, h);
	wl_surface_commit(surface);
	wl_display_roundtrip(display);
}

int
main(int argc, char **argv)
{
	int seconds = argc > 1 ? atoi(argv[1]) : 5;
	struct wl_display *display = wl_display_connect(NULL);
	if (display == NULL) {
		fprintf(stderr, "no Wayland display\n");
		return 1;
	}
	struct wl_registry *registry = wl_display_get_registry(display);
	wl_registry_add_listener(registry, &sRegistryListener, NULL);
	wl_display_roundtrip(display);
	if (!sCompositor || !sShm || !sWmBase || !sTest) {
		fprintf(stderr, "missing globals (koan_test_v1 needs "
			"/run/koan-test-control)\n");
		return 1;
	}
	xdg_wm_base_add_listener(sWmBase, &sWmBaseListener, NULL);

	struct wl_surface *surface = wl_compositor_create_surface(sCompositor);
	struct xdg_surface *xdg = xdg_wm_base_get_xdg_surface(sWmBase, surface);
	struct xdg_toplevel *toplevel = xdg_surface_get_toplevel(xdg);
	xdg_toplevel_set_title(toplevel, "popup follow check");
	bool configured = false;
	map(display, surface, xdg, &configured, 200, 150, 0xff2050c0);

	struct xdg_positioner *positioner = xdg_wm_base_create_positioner(sWmBase);
	xdg_positioner_set_size(positioner, 60, 40);
	xdg_positioner_set_anchor_rect(positioner, 20, 20, 1, 1);
	xdg_positioner_set_anchor(positioner, XDG_POSITIONER_ANCHOR_TOP_LEFT);
	xdg_positioner_set_gravity(positioner, XDG_POSITIONER_GRAVITY_BOTTOM_RIGHT);
	struct wl_surface *psurface = wl_compositor_create_surface(sCompositor);
	struct xdg_surface *pxdg = xdg_wm_base_get_xdg_surface(sWmBase, psurface);
	xdg_surface_get_popup(pxdg, xdg, positioner);
	bool pconfigured = false;
	map(display, psurface, pxdg, &pconfigured, 60, 40, 0xff20c040);

	koan_test_v1_move_surface(sTest, surface, 100, 100);
	test_sync(display);
	printf("PLACED\n");
	fflush(stdout);
	sleep(seconds);
	koan_test_v1_move_surface(sTest, surface, 500, 300);
	test_sync(display);
	wl_display_roundtrip(display);
	printf("MOVED\n");
	fflush(stdout);
	sleep(seconds);
	wl_display_disconnect(display);
	return 0;
}
