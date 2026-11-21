/*
 * Copyright 2026, Koan contributors. All rights reserved.
 * Distributed under the terms of the MIT License.
 */


#include "SchedulerMode.h"

#include <stdlib.h>
#include <string.h>

#include <scheduler.h>

#include <systemd/sd-bus.h>


// power-profiles-daemon, under its current name and its old one
static const char* const kProfileServices[][3] = {
	{ "org.freedesktop.UPower.PowerProfiles",
		"/org/freedesktop/UPower/PowerProfiles",
		"org.freedesktop.UPower.PowerProfiles" },
	{ "net.hadess.PowerProfiles", "/net/hadess/PowerProfiles",
		"net.hadess.PowerProfiles" }
};
static const size_t kProfileServiceCount
	= sizeof(kProfileServices) / sizeof(kProfileServices[0]);

static const char* kPowerSaverProfile = "power-saver";
static const char* kBalancedProfile = "balanced";


/*!	Reads the active profile; returns the index of the service that
	answered, or -1. The caller frees \a _profile. */
static int
get_active_profile(sd_bus* bus, char** _profile)
{
	for (size_t i = 0; i < kProfileServiceCount; i++) {
		sd_bus_error error = SD_BUS_ERROR_NULL;
		char* profile = NULL;
		int status = sd_bus_get_property_string(bus, kProfileServices[i][0],
			kProfileServices[i][1], kProfileServices[i][2], "ActiveProfile",
			&error, &profile);
		sd_bus_error_free(&error);
		if (status >= 0) {
			*_profile = profile;
			return (int)i;
		}
	}
	return -1;
}


int32
pc_get_scheduler_mode()
{
	sd_bus* bus = NULL;
	if (sd_bus_open_system(&bus) < 0)
		return SCHEDULER_MODE_LOW_LATENCY;

	int32 mode = SCHEDULER_MODE_LOW_LATENCY;
	char* profile = NULL;
	if (get_active_profile(bus, &profile) >= 0) {
		if (strcmp(profile, kPowerSaverProfile) == 0)
			mode = SCHEDULER_MODE_POWER_SAVING;
		free(profile);
	}

	sd_bus_unref(bus);
	return mode;
}


status_t
pc_set_scheduler_mode(int32 mode)
{
	if (mode != SCHEDULER_MODE_LOW_LATENCY
		&& mode != SCHEDULER_MODE_POWER_SAVING)
		return B_BAD_VALUE;

	sd_bus* bus = NULL;
	if (sd_bus_open_system(&bus) < 0)
		return B_NOT_SUPPORTED;

	status_t result = B_NOT_SUPPORTED;
	char* current = NULL;
	int service = get_active_profile(bus, &current);
	if (service >= 0) {
		bool powerSaving = strcmp(current, kPowerSaverProfile) == 0;
		free(current);

		result = B_OK;
		if (powerSaving != (mode == SCHEDULER_MODE_POWER_SAVING)) {
			// Leaving power saving selects "balanced"; a "performance"
			// profile already counts as low latency and is kept.
			const char* profile = mode == SCHEDULER_MODE_POWER_SAVING
				? kPowerSaverProfile : kBalancedProfile;
			sd_bus_error error = SD_BUS_ERROR_NULL;
			if (sd_bus_set_property(bus, kProfileServices[service][0],
					kProfileServices[service][1],
					kProfileServices[service][2], "ActiveProfile", &error,
					"s", profile) < 0) {
				result = B_NOT_ALLOWED;
			}
			sd_bus_error_free(&error);
		}
	}

	sd_bus_unref(bus);
	return result;
}
