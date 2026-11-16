/*
 * Copyright 2026, Koan contributors. All rights reserved.
 * Distributed under the terms of the MIT License.
 */


// vos-splash: the Haiku boot splash on Linux.
//
//   vos-splash [ICONS]
//		Draws the logo and the icon row with the first ICONS icons (0..7)
//		lit, and keeps the display until SIGTERM or SIGINT. Exits 0 at once
//		when there is no DRM device and no fbdev to draw on.

#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <boot_splash.h>

#include "splash_display.h"
#include "splash_platform.h"


static volatile sig_atomic_t sTerminate = 0;


static void
on_terminate(int)
{
	sTerminate = 1;
}


int
main(int argc, char** argv)
{
	int icons = 0;
	if (argc > 2 || (argc == 2 && (argv[1][0] < '0' || argv[1][0] > '9'))) {
		fprintf(stderr, "usage: vos-splash [ICONS]\n");
		return 2;
	}
	if (argc == 2) {
		icons = atoi(argv[1]);
		if (icons > BOOT_SPLASH_STAGE_MAX)
			icons = BOOT_SPLASH_STAGE_MAX;
	}

	int devDir = open("/dev", O_PATH | O_DIRECTORY | O_CLOEXEC);
	if (devDir < 0)
		return 0;

	splash_display display;
	if (splash_display_open(&display, devDir) != B_OK)
		return 0;

	uint8* iconImage = NULL;
	if (video_display_splash(display.frame_buffer, &iconImage) != B_OK) {
		splash_display_close(&display, false);
		return 1;
	}
	boot_splash_init(iconImage, &display.frame_buffer);
	for (int i = 0; i < icons; i++)
		boot_splash_set_stage(i);
	splash_display_show(&display);
	splash_display_flush(&display);

	struct sigaction action;
	memset(&action, 0, sizeof(action));
	action.sa_handler = on_terminate;
	sigaction(SIGTERM, &action, NULL);
	sigaction(SIGINT, &action, NULL);
	while (!sTerminate)
		pause();

	boot_splash_uninit();
	free(iconImage);
	splash_display_close(&display, false);
	return 0;
}
