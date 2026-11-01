/*
 * Copyright 2026, Koan contributors
 * SPDX-License-Identifier: Apache-2.0
 *
 * Part of Koan, a VitruvianOS fork
 */

#include "WaylandDataDeviceManager.h"

#include <wayland-client.h>
#include <wayland-server.h>

#include <atomic>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>


struct TestServer {
	wl_display* display;
	std::atomic<bool> running;
	std::atomic<bool> failed;
};


struct TestClient {
	wl_data_device_manager* manager;
	wl_seat* seat;
};


static void
_SeatGetPointer(struct wl_client* client, struct wl_resource* resource,
	uint32_t id)
{
	(void)client;
	(void)resource;
	(void)id;
}


static void
_SeatGetKeyboard(struct wl_client* client, struct wl_resource* resource,
	uint32_t id)
{
	(void)client;
	(void)resource;
	(void)id;
}


static void
_SeatGetTouch(struct wl_client* client, struct wl_resource* resource,
	uint32_t id)
{
	(void)client;
	(void)resource;
	(void)id;
}


static void
_SeatRelease(struct wl_client* client, struct wl_resource* resource)
{
	(void)client;
	wl_resource_destroy(resource);
}


static const struct wl_seat_interface sSeatInterface = {
	_SeatGetPointer,
	_SeatGetKeyboard,
	_SeatGetTouch,
	_SeatRelease
};


static void
_SeatBind(struct wl_client* client, void* data, uint32_t version,
	uint32_t id)
{
	(void)data;
	struct wl_resource* seat = wl_resource_create(client, &wl_seat_interface,
		version, id);
	if (seat == NULL) {
		wl_client_post_no_memory(client);
		return;
	}

	wl_resource_set_implementation(seat, &sSeatInterface, NULL, NULL);
	wl_seat_send_capabilities(seat,
		WL_SEAT_CAPABILITY_POINTER | WL_SEAT_CAPABILITY_KEYBOARD);
}


static void
_HandleGlobal(void* data, struct wl_registry* registry, uint32_t name,
	const char* interface, uint32_t version)
{
	TestClient* client = static_cast<TestClient*>(data);
	if (strcmp(interface, wl_data_device_manager_interface.name) == 0) {
		client->manager = static_cast<wl_data_device_manager*>(
			wl_registry_bind(registry, name,
				&wl_data_device_manager_interface, version));
	} else if (strcmp(interface, wl_seat_interface.name) == 0) {
		client->seat = static_cast<wl_seat*>(
			wl_registry_bind(registry, name, &wl_seat_interface, version));
	}
}


static void
_HandleGlobalRemove(void* data, struct wl_registry* registry, uint32_t name)
{
	(void)data;
	(void)registry;
	(void)name;
}


static const struct wl_registry_listener sRegistryListener = {
	_HandleGlobal,
	_HandleGlobalRemove
};


static void*
_DispatchServer(void* data)
{
	TestServer* server = static_cast<TestServer*>(data);
	wl_event_loop* loop = wl_display_get_event_loop(server->display);
	while (server->running.load()) {
		if (wl_event_loop_dispatch(loop, 10) < 0) {
			server->failed.store(true);
			break;
		}
		wl_display_flush_clients(server->display);
	}
	return NULL;
}


int
main()
{
	wl_display* display = wl_display_create();
	if (display == NULL)
		return 1;

	wl_global* managerGlobal = CreateWaylandDataDeviceManagerGlobal(display);
	wl_global* seatGlobal = wl_global_create(display, &wl_seat_interface, 1,
		NULL, _SeatBind);
	if (managerGlobal == NULL || seatGlobal == NULL) {
		wl_display_destroy(display);
		return 1;
	}

	int sockets[2];
	if (socketpair(AF_UNIX, SOCK_STREAM, 0, sockets) != 0) {
		wl_display_destroy(display);
		return 1;
	}

	if (wl_client_create(display, sockets[0]) == NULL) {
		close(sockets[0]);
		close(sockets[1]);
		wl_display_destroy(display);
		return 1;
	}

	wl_display* clientDisplay = wl_display_connect_to_fd(sockets[1]);
	if (clientDisplay == NULL) {
		wl_display_destroy_clients(display);
		wl_display_destroy(display);
		return 1;
	}

	TestServer server = {display, true, false};
	pthread_t serverThread;
	if (pthread_create(&serverThread, NULL, _DispatchServer, &server) != 0) {
		wl_display_disconnect(clientDisplay);
		wl_display_destroy_clients(display);
		wl_display_destroy(display);
		return 1;
	}

	TestClient client = {NULL, NULL};
	wl_registry* registry = wl_display_get_registry(clientDisplay);
	wl_registry_add_listener(registry, &sRegistryListener, &client);
	bool passed = wl_display_roundtrip(clientDisplay) >= 0
		&& client.manager != NULL && client.seat != NULL;

	wl_data_device* device = NULL;
	wl_data_source* source = NULL;
	if (passed) {
		device = wl_data_device_manager_get_data_device(client.manager,
			client.seat);
		source = wl_data_device_manager_create_data_source(client.manager);
		passed = device != NULL && source != NULL;
	}
	if (passed) {
		wl_data_source_offer(source, "text/plain");
		passed = wl_display_roundtrip(clientDisplay) >= 0;
	}

	if (source != NULL)
		wl_data_source_destroy(source);
	if (device != NULL)
		wl_data_device_destroy(device);
	if (client.manager != NULL)
		wl_data_device_manager_destroy(client.manager);
	if (client.seat != NULL)
		wl_seat_destroy(client.seat);
	if (registry != NULL)
		wl_registry_destroy(registry);
	wl_display_roundtrip(clientDisplay);
	wl_display_disconnect(clientDisplay);

	server.running.store(false);
	pthread_join(serverThread, NULL);
	passed = passed && !server.failed.load();
	wl_display_destroy_clients(display);
	wl_display_destroy(display);

	if (!passed)
		fprintf(stderr, "Wayland data device manager protocol test failed\n");

	return passed ? 0 : 1;
}
