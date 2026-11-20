/*
 * Copyright 2026, air/OS. All rights reserved.
 * Distributed under the terms of the MIT License.
 */


#include "linux_power.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <systemd/sd-bus.h>

#include "power_policy.h"


static const char* kLogindService = "org.freedesktop.login1";
static const char* kLogindPath = "/org/freedesktop/login1";
static const char* kLogindManager = "org.freedesktop.login1.Manager";

static const char* kUPowerService = "org.freedesktop.UPower";
static const char* kUPowerPath = "/org/freedesktop/UPower";
static const char* kUPowerInterface = "org.freedesktop.UPower";


/*!	Reads a boolean property of UPower; returns -1 when there is no UPower
	or no such property. */
static int
upower_bool(sd_bus* bus, const char* property)
{
	sd_bus_error error = SD_BUS_ERROR_NULL;
	int value = 0;
	int status = sd_bus_get_property_trivial(bus, kUPowerService, kUPowerPath,
		kUPowerInterface, property, &error, 'b', &value);
	sd_bus_error_free(&error);
	if (status < 0)
		return -1;
	return value ? 1 : 0;
}


int
linux_power_inhibit(const char* what, const char* why)
{
	sd_bus* bus = NULL;
	if (sd_bus_open_system(&bus) < 0)
		return -1;

	sd_bus_error error = SD_BUS_ERROR_NULL;
	sd_bus_message* reply = NULL;
	int fd = -1;
	int status = sd_bus_call_method(bus, kLogindService, kLogindPath,
		kLogindManager, "Inhibit", &error, &reply, "ssss", what,
		"power_daemon", why, "block");
	if (status >= 0) {
		int busFD;
		if (sd_bus_message_read(reply, "h", &busFD) >= 0)
			fd = fcntl(busFD, F_DUPFD_CLOEXEC, 3);
			// (the message owns the descriptor it carries)
	} else {
		fprintf(stderr, "power_daemon: cannot take the %s lock: %s\n", what,
			error.message != NULL ? error.message : strerror(-status));
	}

	sd_bus_message_unref(reply);
	sd_bus_error_free(&error);
	sd_bus_unref(bus);
	return fd;
}


static int
prepare_for_sleep(sd_bus_message* message, void* data, sd_bus_error* error)
{
	int start;
	if (sd_bus_message_read(message, "b", &start) >= 0)
		*(int*)data = start ? 1 : 2;
	return 0;
}


status_t
linux_power_suspend()
{
	sd_bus* bus = NULL;
	if (sd_bus_open_system(&bus) < 0)
		return B_ERROR;

	// 0: nothing yet, 1: going to sleep, 2: woke up
	int state = 0;
	sd_bus_slot* slot = NULL;
	int status = sd_bus_match_signal(bus, &slot, kLogindService, kLogindPath,
		kLogindManager, "PrepareForSleep", prepare_for_sleep, &state);

	sd_bus_error error = SD_BUS_ERROR_NULL;
	if (status >= 0) {
		status = sd_bus_call_method(bus, kLogindService, kLogindPath,
			kLogindManager, "Suspend", &error, NULL, "b", 0);
	}
	if (status < 0) {
		printf("power_daemon: cannot sleep: %s\n", error.message != NULL
			? error.message : strerror(-status));
		sd_bus_error_free(&error);
		sd_bus_slot_unref(slot);
		sd_bus_unref(bus);
		return B_ERROR;
	}
	sd_bus_error_free(&error);

	// logind sends PrepareForSleep(true) before the machine sleeps, and
	// PrepareForSleep(false) once it woke up. Should the sleep not happen
	// at all (another program's inhibitor lock), the daemon does not hang:
	// without the first signal it gives up after a while.
	bigtime_t giveUp = system_time() + 30000000;
	while (state != 2) {
		status = sd_bus_process(bus, NULL);
		if (status < 0)
			break;
		if (status > 0)
			continue;
		if (state == 0 && system_time() > giveUp)
			break;
		sd_bus_wait(bus, state == 0 ? 1000000 : UINT64_MAX);
	}

	sd_bus_slot_unref(slot);
	sd_bus_unref(bus);
	return state == 2 ? B_OK : B_ERROR;
}


int
linux_power_lid_closed()
{
	sd_bus* bus = NULL;
	if (sd_bus_open_system(&bus) < 0)
		return -1;

	int closed = -1;
	if (upower_bool(bus, "LidIsPresent") == 1)
		closed = upower_bool(bus, "LidIsClosed");

	sd_bus_unref(bus);
	return closed;
}


void
linux_power_set_power_saving(bool powerSaving)
{
	// power-profiles-daemon, under its current name and its old one
	static const char* const kProfiles[][3] = {
		{ "org.freedesktop.UPower.PowerProfiles",
			"/org/freedesktop/UPower/PowerProfiles",
			"org.freedesktop.UPower.PowerProfiles" },
		{ "net.hadess.PowerProfiles", "/net/hadess/PowerProfiles",
			"net.hadess.PowerProfiles" }
	};

	sd_bus* bus = NULL;
	if (sd_bus_open_system(&bus) < 0)
		return;

	const char* profile = powerSaving ? "power-saver" : "balanced";
	for (size_t i = 0; i < sizeof(kProfiles) / sizeof(kProfiles[0]); i++) {
		sd_bus_error error = SD_BUS_ERROR_NULL;
		char* current = NULL;
		int status = sd_bus_get_property_string(bus, kProfiles[i][0],
			kProfiles[i][1], kProfiles[i][2], "ActiveProfile", &error,
			&current);
		sd_bus_error_free(&error);
		if (status < 0)
			continue;

		bool same = strcmp(current, profile) == 0;
		free(current);
		if (!same) {
			status = sd_bus_set_property(bus, kProfiles[i][0],
				kProfiles[i][1], kProfiles[i][2], "ActiveProfile", &error,
				"s", profile);
			if (status < 0) {
				printf("power_daemon: cannot select the %s profile: %s\n",
					profile, error.message != NULL ? error.message
						: strerror(-status));
			}
			sd_bus_error_free(&error);
		}
		break;
	}

	sd_bus_unref(bus);
}


// #pragma mark - LinuxPowerMonitor


LinuxPowerMonitor::LinuxPowerMonitor(PowerPolicy* policy)
	:
	fPolicy(policy),
	fBus(NULL),
	fLidInhibitor(-1),
	fChanged(false)
{
	if (sd_bus_open_system(&fBus) < 0) {
		fBus = NULL;
		return;
	}

	if (sd_bus_match_signal(fBus, NULL, kUPowerService, kUPowerPath,
			"org.freedesktop.DBus.Properties", "PropertiesChanged",
			&_PropertiesChanged, this) < 0
		|| upower_bool(fBus, "OnBattery") < 0) {
		// no UPower: nothing to watch
		sd_bus_unref(fBus);
		fBus = NULL;
		return;
	}

	if (upower_bool(fBus, "LidIsPresent") == 1) {
		fLidInhibitor = linux_power_inhibit("handle-lid-switch",
			"The Power preferences choose what closing the lid does");
	}

	int fd = sd_bus_get_fd(fBus);
	if (fd >= 0)
		fFDs.insert(fd);

	_Update();
}


LinuxPowerMonitor::~LinuxPowerMonitor()
{
	if (fLidInhibitor >= 0)
		close(fLidInhibitor);
	sd_bus_flush_close_unref(fBus);
}


void
LinuxPowerMonitor::HandleEvent(int fd)
{
	// The policy may put the machine to sleep and wait for it to wake up,
	// so it is told only after the bus has been processed: sd-bus cannot
	// be processed again from within its own callbacks.
	while (sd_bus_process(fBus, NULL) > 0)
		;

	if (fChanged) {
		fChanged = false;
		_Update();
	}
}


/*static*/ int
LinuxPowerMonitor::_PropertiesChanged(sd_bus_message* message, void* data,
	sd_bus_error* error)
{
	((LinuxPowerMonitor*)data)->fChanged = true;
	return 0;
}


void
LinuxPowerMonitor::_Update()
{
	int onBattery = upower_bool(fBus, "OnBattery");
	if (onBattery >= 0)
		fPolicy->SetOnAC(onBattery == 0);

	if (upower_bool(fBus, "LidIsPresent") == 1) {
		int closed = upower_bool(fBus, "LidIsClosed");
		if (closed >= 0)
			fPolicy->SetLidOpen(closed == 0);
	}
}
