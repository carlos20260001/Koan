/*
 * Copyright 2026, Koan contributors
 * SPDX-License-Identifier: Apache-2.0
 *
 * Part of Koan, a VitruvianOS fork
 */

#include "WaylandDataDeviceManager.h"

#include "WaylandResourceUtils.h"

#include <wayland-server.h>

#include <string.h>


static void
_DataSourceOffer(struct wl_client* client, struct wl_resource* resource,
	const char* mimeType)
{
	// Clipboard and drag-and-drop are not wired into the native desktop yet.
	(void)client;
	(void)resource;
	(void)mimeType;
}


static void
_DataSourceDestroy(struct wl_client* client, struct wl_resource* resource)
{
	(void)client;
	wl_resource_destroy(resource);
}


static void
_DataSourceSetActions(struct wl_client* client, struct wl_resource* resource,
	uint32_t actions)
{
	(void)client;
	(void)resource;
	(void)actions;
}


static const struct wl_data_source_interface sDataSourceInterface = {
	_DataSourceOffer,
	_DataSourceDestroy,
	_DataSourceSetActions
};


static void
_DataDeviceStartDrag(struct wl_client* client, struct wl_resource* resource,
	struct wl_resource* source, struct wl_resource* origin,
	struct wl_resource* icon, uint32_t serial)
{
	(void)client;
	(void)resource;
	(void)source;
	(void)origin;
	(void)icon;
	(void)serial;
}


static void
_DataDeviceSetSelection(struct wl_client* client,
	struct wl_resource* resource, struct wl_resource* source,
	uint32_t serial)
{
	(void)client;
	(void)resource;
	(void)source;
	(void)serial;
}


static void
_DataDeviceRelease(struct wl_client* client, struct wl_resource* resource)
{
	(void)client;
	wl_resource_destroy(resource);
}


static const struct wl_data_device_interface sDataDeviceInterface = {
	_DataDeviceStartDrag,
	_DataDeviceSetSelection,
	_DataDeviceRelease
};


static void
_CreateDataSource(struct wl_client* client, struct wl_resource* resource,
	uint32_t id)
{
	struct wl_resource* source = wl_resource_create(client,
		&wl_data_source_interface, wl_resource_get_version(resource), id);
	if (source == NULL) {
		wl_client_post_no_memory(client);
		return;
	}

	wl_resource_set_implementation(source, &sDataSourceInterface, NULL, NULL);
}


static void
_GetDataDevice(struct wl_client* client, struct wl_resource* resource,
	uint32_t id, struct wl_resource* seat)
{
	if (!WaylandResourceBelongsToClient(seat, client)
		|| strcmp(wl_resource_get_class(seat), "wl_seat") != 0) {
		wl_resource_post_error(resource, WL_DISPLAY_ERROR_INVALID_OBJECT,
			"get_data_device requires a wl_seat owned by this client");
		return;
	}

	struct wl_resource* device = wl_resource_create(client,
		&wl_data_device_interface, wl_resource_get_version(resource), id);
	if (device == NULL) {
		wl_client_post_no_memory(client);
		return;
	}

	wl_resource_set_implementation(device, &sDataDeviceInterface, NULL, NULL);
}


static const struct wl_data_device_manager_interface
sDataDeviceManagerInterface = {
	_CreateDataSource,
	_GetDataDevice
};


static void
_DataDeviceManagerBind(struct wl_client* client, void* data,
	uint32_t version, uint32_t id)
{
	(void)data;
	struct wl_resource* manager = wl_resource_create(client,
		&wl_data_device_manager_interface, version, id);
	if (manager == NULL) {
		wl_client_post_no_memory(client);
		return;
	}

	wl_resource_set_implementation(manager, &sDataDeviceManagerInterface,
		NULL, NULL);
}


struct wl_global*
CreateWaylandDataDeviceManagerGlobal(struct wl_display* display)
{
	if (display == NULL)
		return NULL;

	return wl_global_create(display, &wl_data_device_manager_interface,
		1, NULL, _DataDeviceManagerBind);
}
