/*
 * Copyright 2026, Koan contributors. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef VOS_SPLASH_STAGES_H
#define VOS_SPLASH_STAGES_H


// The Haiku kernel lit one icon per BOOT_SPLASH_STAGE_* while it booted.
// On Linux the same seven icons follow these boot points; the numbers are
// what `vos-splash --stage N` and the FIFO take.
enum {
	VOS_SPLASH_STAGE_INITRAMFS = 1,
		// initramfs init-top: the daemon starts
	VOS_SPLASH_STAGE_ROOT_MOUNTED,
		// initramfs init-bottom: the real root is mounted
	VOS_SPLASH_STAGE_SYSINIT,
		// systemd reached sysinit.target
	VOS_SPLASH_STAGE_UDEV_SETTLED,
		// udev processed the coldplug events
	VOS_SPLASH_STAGE_BASIC,
		// systemd reached basic.target
	VOS_SPLASH_STAGE_REGISTRAR,
		// registrar.service started
	VOS_SPLASH_STAGE_APP_SERVER,
		// app_server is about to take the display: hand off

	VOS_SPLASH_STAGE_COUNT = VOS_SPLASH_STAGE_APP_SERVER
};


// Returns the BOOT_SPLASH_STAGE_* icon for a Linux stage, -1 if invalid.
int vos_splash_stage_icon(int stage);

// Parses a stage number ("1".."7"), -1 if it is not one.
int vos_splash_parse_stage(const char* text);


#endif	/* VOS_SPLASH_STAGES_H */
