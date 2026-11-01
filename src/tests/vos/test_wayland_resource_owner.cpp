/*
 * Copyright 2026, Koan contributors
 * SPDX-License-Identifier: Apache-2.0
 *
 * Part of Koan, a VitruvianOS fork
 */


#include "WaylandResourceUtils.h"

#include <wayland-server.h>

#include <sys/socket.h>
#include <unistd.h>

#include <stdio.h>


int
main()
{
	wl_display* display = wl_display_create();
	if (display == NULL)
		return 1;

	int firstSockets[2];
	int secondSockets[2];
	if (socketpair(AF_UNIX, SOCK_STREAM, 0, firstSockets) != 0) {
		wl_display_destroy(display);
		return 1;
	}
	if (socketpair(AF_UNIX, SOCK_STREAM, 0, secondSockets) != 0) {
		close(firstSockets[0]);
		close(firstSockets[1]);
		wl_display_destroy(display);
		return 1;
	}

	wl_client* firstClient = wl_client_create(display, firstSockets[0]);
	wl_client* secondClient = wl_client_create(display, secondSockets[0]);
	wl_resource* firstResource = firstClient != NULL
		? wl_resource_create(firstClient, &wl_seat_interface, 1, 0) : NULL;
	wl_resource* secondResource = secondClient != NULL
		? wl_resource_create(secondClient, &wl_seat_interface, 1, 0) : NULL;

	bool passed = firstResource != NULL && secondResource != NULL
		&& WaylandResourceBelongsToClient(firstResource, firstClient)
		&& !WaylandResourceBelongsToClient(firstResource, secondClient)
		&& WaylandResourceBelongsToClient(secondResource, secondClient)
		&& !WaylandResourceBelongsToClient(NULL, firstClient)
		&& !WaylandResourceBelongsToClient(firstResource, NULL);

	wl_display_destroy_clients(display);
	wl_display_destroy(display);
	close(firstSockets[1]);
	close(secondSockets[1]);

	if (!passed)
		fprintf(stderr, "Wayland resource client ownership test failed\n");

	return passed ? 0 : 1;
}
