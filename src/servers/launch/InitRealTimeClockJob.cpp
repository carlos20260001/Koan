/*
 * Copyright 2015, Axel Dörfler, axeld@pinc-software.de.
 * Copyright 2004, Jérôme Duval, jerome.duval@free.fr.
 * Copyright 2010, 2012, Oliver Tappe <zooey@hirschkaefer.de>
 * Distributed under the terms of the MIT License.
 */


//! Initialize real time clock, and time zone offset


#include "InitRealTimeClockJob.h"

#include <stdio.h>

#include <File.h>
#include <FindDirectory.h>
#include <Message.h>
#include <TimeZone.h>



using BSupportKit::BJob;


InitRealTimeClockJob::InitRealTimeClockJob()
	:
	BJob("init real time clock")
{
}


status_t
InitRealTimeClockJob::Execute()
{
	BPath path;
	status_t status = find_directory(B_USER_SETTINGS_DIRECTORY, &path);
	if (status == B_OK) {
		_SetRealTimeClockIsGMT(path);
		_SetTimeZoneOffset(path);
	}
	return status;
}


void
InitRealTimeClockJob::_SetRealTimeClockIsGMT(BPath path) const
{
	path.Append("RTC_time_settings");
	BFile file;
	status_t status = file.SetTo(path.Path(), B_READ_ONLY);
	if (status != B_OK) {
		fprintf(stderr, "Can't open RTC settings file\n");
		return;
	}

	char buffer[10];
	ssize_t bytesRead = file.Read(buffer, sizeof(buffer));
	if (bytesRead < 0) {
		fprintf(stderr, "Unable to read RTC settings file\n");
		return;
	}
	bool isGMT = strncmp(buffer, "local", 5) != 0;

	// TODO(linux backend): Linux keeps this in /etc/adjtime, which
	// systemd-timedated's SetLocalRTC (D-Bus, polkit) changes; Haiku's
	// _kern_set_real_time_clock_is_gmt() has no counterpart in libroot2.
	printf("RTC stores %s time.\n", isGMT ? "GMT" : "local" );
}


void
InitRealTimeClockJob::_SetTimeZoneOffset(BPath path) const
{
	path.Append("Time settings");
	BFile file;
	status_t status = file.SetTo(path.Path(), B_READ_ONLY);
	if (status != B_OK) {
		fprintf(stderr, "Can't open Time settings file\n");
		return;
	}

	BMessage settings;
	status = settings.Unflatten(&file);
	if (status != B_OK) {
		fprintf(stderr, "Unable to parse Time settings file\n");
		return;
	}
	BString timeZoneID;
	if (settings.FindString("timezone", &timeZoneID) != B_OK) {
		fprintf(stderr, "No timezone found\n");
		return;
	}
	int32 timeZoneOffset = BTimeZone(timeZoneID.String()).OffsetFromGMT();

	// TODO(linux backend): the system time zone is /etc/localtime, set
	// through systemd-timedated's SetTimezone (D-Bus, polkit); processes
	// read it themselves, there is no _kern_set_timezone() on Linux.
	printf("timezone is %s, offset is %" B_PRId32 " seconds from GMT.\n",
		timeZoneID.String(), timeZoneOffset);
}
