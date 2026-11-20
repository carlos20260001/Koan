/*
 * Copyright 2005-2014, Haiku, Inc.
 * Distributed under the terms of the MIT license.
 *
 * Authors:
 *		Rene Gollent
 *		Nathan Whitehorn
 */


#include "power_button_monitor.h"

#include "power_policy.h"

#include <string.h>

#include <Directory.h>
#include <Messenger.h>
#include <Roster.h>
#include <String.h>

#include <RosterPrivate.h>

#ifdef __VOS__
#	include <fcntl.h>
#	include <unistd.h>
#	include <linux/input.h>
#	include <sys/ioctl.h>

#	include "linux_power.h"
#endif


#ifdef __VOS__
static const char* kBasePath = "/dev/input";


static bool
has_power_key(int fd)
{
	unsigned long keys[KEY_MAX / (8 * sizeof(long)) + 1] = {};
	if (ioctl(fd, EVIOCGBIT(EV_KEY, sizeof(keys)), keys) < 0)
		return false;
	return (keys[KEY_POWER / (8 * sizeof(long))]
		& (1UL << (KEY_POWER % (8 * sizeof(long))))) != 0;
}
#else
static const char* kBasePath = "/dev/power/button";
#endif


PowerButtonMonitor::PowerButtonMonitor(PowerPolicy* policy)
	:
	fFDs(),
	fPolicy(policy)
{
#ifdef __VOS__
	// The power key is an evdev key (the ACPI "Power Button" device, and
	// keyboards that have one). Reading it takes access to the input
	// devices; when the daemon has it, it holds logind's
	// "handle-power-key" lock, so the press is the daemon's to handle (the
	// shut-down dialog) and not logind's. Otherwise logind keeps handling
	// the key as configured in logind.conf.
	// TODO(linux backend): input devices plugged in later are not watched.
	fInhibitor = -1;
	BDirectory dir;
	if (dir.SetTo(kBasePath) != B_OK)
		return;

	entry_ref ref;
	while (dir.GetNextRef(&ref) == B_OK) {
		if (strncmp(ref.name, "event", 5) != 0)
			continue;
		BString path;
		path.SetToFormat("%s/%s", kBasePath, ref.name);
		int fd = open(path.String(), O_RDONLY | O_NONBLOCK | O_CLOEXEC);
		if (fd < 0)
			continue;
		if (has_power_key(fd))
			fFDs.insert(fd);
		else
			close(fd);
	}

	if (!fFDs.empty()) {
		fInhibitor = linux_power_inhibit("handle-power-key",
			"The power key opens the shut down dialog");
	}
#else
	BDirectory dir;
	if (dir.SetTo(kBasePath) != B_OK)
		return;

	entry_ref ref;
	while (dir.GetNextRef(&ref) == B_OK) {
		if (strncmp(ref.name, "power", 5) == 0) {
			BString path;
			path.SetToFormat("%s/%s", kBasePath, ref.name);
			int fd = open(path.String(), O_RDONLY);
			if (fd > 0)
				fFDs.insert(fd);
		}
	}
#endif
}


PowerButtonMonitor::~PowerButtonMonitor()
{
#ifdef __VOS__
	if (fInhibitor >= 0)
		close(fInhibitor);
#endif
	for (std::set<int>::iterator it = fFDs.begin(); it != fFDs.end(); ++it)
		close(*it);
}


void
PowerButtonMonitor::HandleEvent(int fd)
{
	uint8 button_pressed = 0;
#ifdef __VOS__
	struct input_event event;
	while (read(fd, &event, sizeof(event)) == (ssize_t)sizeof(event)) {
		if (event.type == EV_KEY && event.code == KEY_POWER
			&& event.value == 1)
			button_pressed = 1;
	}
#else
	if (read(fd, &button_pressed, 1) != 1)
		return;
#endif

	// (the press that woke the machine is not one to shut it down)
	if (button_pressed && !fPolicy->JustWokeUp()) {
		BRoster roster;
		BRoster::Private rosterPrivate(roster);

		rosterPrivate.ShutDown(false, false, false);
	}
}
