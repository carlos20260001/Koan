/*
 * Copyright 2026, Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef _LOCATION_SERVER_H
#define _LOCATION_SERVER_H

/*!	The messages location_server understands; see docs/airos/LOCATION.md. */

#define LOCATION_SERVER_SIGNATURE	"application/x-vnd.vos-location_server"

enum {
	LOCATION_GET_POSITION	= 'lcgp'
		// "max_age" (int64, microseconds, optional, default 60 s): a position
		// determined this long ago at most will do.
		// Reply: "status" (int32). With B_OK: "latitude", "longitude"
		// (double, degrees, WGS 84), "accuracy" (double, meters, the radius),
		// "source" (string: "fixed", "wifi" or "ip"), "time" (int64,
		// real_time_clock_usecs() when it was determined).
		// B_NOT_ALLOWED: location is switched off in the settings.
};

#endif	// _LOCATION_SERVER_H
