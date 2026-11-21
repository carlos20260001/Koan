/*
 * Copyright 2026, Koan contributors. All rights reserved.
 * Distributed under the terms of the MIT License.
 */


#include "TimeBackend.h"

#include <stdio.h>
#include <string.h>

#include <BusHelpers.h>


namespace TimeBackend {


status_t
SetSystemTime(time_t time, BString* error)
{
	// timedated refuses while its own NTP synchronisation is on; the panel
	// then reports the error instead of fighting the time service.
	status_t status = BPrivate::bus_timedate1_set_time(
		(int64)time * 1000000LL, false, error);
	if (status != B_OK)
		fprintf(stderr, "Time: setting the clock failed: %s\n",
			error != NULL ? error->String() : strerror(status));
	return status;
}


status_t
SetTimeZone(const char* id, BString* error)
{
	return BPrivate::bus_timedate1_set_timezone(id, error);
}


bool
RealTimeClockIsGMT()
{
	bool local = false;
	if (BPrivate::bus_timedate1_get_local_rtc(local) != B_OK)
		return true;
	return !local;
}


status_t
SetRealTimeClockIsGMT(bool isGMT, BString* error)
{
	// fix_system = false: the system clock stays right, timedated rewrites
	// the RTC in the new mode.
	return BPrivate::bus_timedate1_set_local_rtc(!isGMT, false, true, error);
}


}	// namespace TimeBackend
