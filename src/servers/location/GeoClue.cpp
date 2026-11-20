/*
 * Copyright 2026, air/OS. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

/*!	The Linux position source: GeoClue 2 over the system D-Bus. GeoClue
	knows the Wi-Fi networks, a GPS or a modem where there is one, and asks
	a geolocation service itself; the location server asks it first and
	does its own lookup only without it.
*/


#include "GeoClue.h"

#include <stdio.h>
#include <string.h>

#include <systemd/sd-bus.h>


static const char* kService = "org.freedesktop.GeoClue2";
static const char* kManagerPath = "/org/freedesktop/GeoClue2/Manager";
static const char* kManager = "org.freedesktop.GeoClue2.Manager";
static const char* kClient = "org.freedesktop.GeoClue2.Client";
static const char* kLocation = "org.freedesktop.GeoClue2.Location";

// GeoClue's GClueAccuracyLevel: EXACT
static const uint32 kAccuracyExact = 8;


struct location_update {
	char	path[256];
	bool	received;
};


static int
location_updated(sd_bus_message* message, void* data, sd_bus_error* error)
{
	location_update* update = (location_update*)data;
	const char* oldPath;
	const char* newPath;
	if (sd_bus_message_read(message, "oo", &oldPath, &newPath) >= 0
		&& strcmp(newPath, "/") != 0) {
		strlcpy(update->path, newPath, sizeof(update->path));
		update->received = true;
	}
	return 0;
}


static bool
location_double(sd_bus* bus, const char* path, const char* name,
	double& value)
{
	sd_bus_error error = SD_BUS_ERROR_NULL;
	int status = sd_bus_get_property_trivial(bus, kService, path, kLocation,
		name, &error, 'd', &value);
	sd_bus_error_free(&error);
	return status >= 0;
}


status_t
geoclue_position(bigtime_t timeout, double& latitude, double& longitude,
	double& accuracy)
{
	sd_bus* bus = NULL;
	if (sd_bus_open_system(&bus) < 0)
		return B_ERROR;

	status_t result = B_ERROR;
	sd_bus_error error = SD_BUS_ERROR_NULL;
	sd_bus_message* reply = NULL;
	sd_bus_slot* slot = NULL;
	const char* clientPath = NULL;
	char client[256] = "";
	location_update update = {};

	int status = sd_bus_call_method(bus, kService, kManagerPath, kManager,
		"GetClient", &error, &reply, "");
	if (status < 0 || sd_bus_message_read(reply, "o", &clientPath) < 0)
		goto out;
	strlcpy(client, clientPath, sizeof(client));

	// GeoClue lets a client in by its desktop ID (its agent may ask the
	// user once per application).
	if (sd_bus_set_property(bus, kService, client, kClient, "DesktopId",
			&error, "s", "x-vnd.vos-location_server") < 0
		|| sd_bus_set_property(bus, kService, client, kClient,
			"RequestedAccuracyLevel", &error, "u", kAccuracyExact) < 0)
		goto out;

	if (sd_bus_match_signal(bus, &slot, kService, client, kClient,
			"LocationUpdated", location_updated, &update) < 0)
		goto out;

	if (sd_bus_call_method(bus, kService, client, kClient, "Start", &error,
			NULL, "") < 0)
		goto out;

	{
		bigtime_t deadline = system_time() + timeout;
		while (!update.received) {
			status = sd_bus_process(bus, NULL);
			if (status < 0)
				break;
			if (status > 0)
				continue;
			bigtime_t left = deadline - system_time();
			if (left <= 0)
				break;
			sd_bus_wait(bus, left);
		}
	}

	if (update.received
		&& location_double(bus, update.path, "Latitude", latitude)
		&& location_double(bus, update.path, "Longitude", longitude)) {
		if (!location_double(bus, update.path, "Accuracy", accuracy))
			accuracy = 0;
		result = B_OK;
	}

	sd_bus_call_method(bus, kService, client, kClient, "Stop", NULL, NULL,
		"");

out:
	if (result != B_OK && error.message != NULL)
		fprintf(stderr, "location_server: GeoClue: %s\n", error.message);
	sd_bus_slot_unref(slot);
	sd_bus_message_unref(reply);
	sd_bus_error_free(&error);
	sd_bus_unref(bus);
	return result;
}
