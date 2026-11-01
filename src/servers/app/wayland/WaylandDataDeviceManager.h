/*
 * Copyright 2026, Koan contributors
 * SPDX-License-Identifier: Apache-2.0
 *
 * Part of Koan, a VitruvianOS fork
 */

#ifndef WAYLAND_DATA_DEVICE_MANAGER_H
#define WAYLAND_DATA_DEVICE_MANAGER_H

struct wl_display;
struct wl_global;

struct wl_global* CreateWaylandDataDeviceManagerGlobal(
	struct wl_display* display);

#endif
