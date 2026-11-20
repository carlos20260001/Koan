/*
 * Copyright 2026, air/OS. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef GEOCLUE_H
#define GEOCLUE_H


#include <OS.h>


/*!	Asks GeoClue 2 for the position, waiting at most \a timeout. Returns
	B_OK, or an error without GeoClue or without a position. */
status_t	geoclue_position(bigtime_t timeout, double& latitude,
				double& longitude, double& accuracy);


#endif	// GEOCLUE_H
