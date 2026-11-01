/*
 * Copyright 2026, Koan contributors
 * SPDX-License-Identifier: Apache-2.0
 *
 * Part of Koan, a VitruvianOS fork
 */


#ifndef VITRUVIAN_WAYLAND_RESOURCE_UTILS_H
#define VITRUVIAN_WAYLAND_RESOURCE_UTILS_H


#include <wayland-server-core.h>


static inline bool
WaylandResourceBelongsToClient(struct wl_resource* resource,
	struct wl_client* client)
{
	return resource != NULL && client != NULL
		&& wl_resource_get_client(resource) == client;
}


#endif // VITRUVIAN_WAYLAND_RESOURCE_UTILS_H
