/*
 * Copyright 2026, Koan contributors. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef TIME_BACKEND_H
#define TIME_BACKEND_H


#include <time.h>

#include <String.h>
#include <SupportDefs.h>


/*!	The Linux side of the Time preferences. Haiku set the clock, the time
	zone and the RTC mode through kernel calls; here systemd-timedated
	(org.freedesktop.timedate1) does it, so polkit asks for the
	administrator's consent where the policy wants it. timedated writes the
	RTC itself (SetTime, SetLocalRTC).
*/
namespace TimeBackend {

status_t	SetSystemTime(time_t time, BString* error = NULL);
status_t	SetTimeZone(const char* id, BString* error = NULL);

// Whether the hardware clock keeps UTC ("GMT" in the panel). Without
// timedated the Linux default, UTC, is reported.
bool		RealTimeClockIsGMT();
status_t	SetRealTimeClockIsGMT(bool isGMT, BString* error = NULL);

}	// namespace TimeBackend


#endif	// TIME_BACKEND_H
