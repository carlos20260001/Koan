/*
 * Copyright 2026, Koan contributors. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef PC_SCHEDULER_MODE_H
#define PC_SCHEDULER_MODE_H


#include <SupportDefs.h>


/*!	Haiku's scheduler modes on Linux: libroot2 has no get_scheduler_mode()
	or set_scheduler_mode(), and Linux's own counterpart is
	power-profiles-daemon's active profile. SCHEDULER_MODE_POWER_SAVING is
	the "power-saver" profile, SCHEDULER_MODE_LOW_LATENCY any other one
	("balanced" when it is selected from here). Without the daemon the mode
	reads as low latency and cannot be changed. */
int32		pc_get_scheduler_mode();
status_t	pc_set_scheduler_mode(int32 mode);


#endif	// PC_SCHEDULER_MODE_H
