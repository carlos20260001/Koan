/*
 * Copyright 2026, Koan contributors. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef SPLASH_HANDOFF_H
#define SPLASH_HANDOFF_H


// Tells the boot splash daemon (vos-splash, M7) to quit once app_server has
// set its first mode: the screen is app_server's from then on. Only the
// first call does anything; nothing happens when no splash is running.
void	vos_splash_handoff();


#endif	// SPLASH_HANDOFF_H
