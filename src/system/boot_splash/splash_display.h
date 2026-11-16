/*
 * Copyright 2026, Koan contributors. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef VOS_SPLASH_DISPLAY_H
#define VOS_SPLASH_DISPLAY_H


#include <sys/types.h>

#include "splash_platform.h"


enum splash_display_kind {
	SPLASH_DISPLAY_NONE = 0,
	SPLASH_DISPLAY_DRM,
	SPLASH_DISPLAY_FBDEV
};


struct splash_display {
	splash_display_kind	kind;
	int					fd;
	int					tty;
		// /dev/tty0 while it is in KD_GRAPHICS, else -1
	dev_t				device;
	char				name[32];
		// "dri/card0" or "fb0", relative to the /dev directory fd
	splash_frame_buffer	frame_buffer;

	void*				map;
	size_t				mapSize;

	// DRM only
	uint32				crtc;
	uint32				connector;
	uint32				fbId;
	uint32				handle;
	bool				master;
	unsigned char		mode[68];
		// struct drm_mode_modeinfo, kept opaque to keep <drm/*.h> local
};


// devDir is an O_PATH/O_DIRECTORY fd on /dev, opened before the initramfs
// pivots: devtmpfs moves to the new root, while the path "/dev" of a
// process started in the initramfs keeps naming the dead initramfs.
//
// Tries DRM dumb buffers on dri/card0..7, then fb0. Maps the frame buffer
// but does not show it yet (splash_display_show()), so the first frame on
// screen is a complete one. Returns B_ENTRY_NOT_FOUND when there is neither.
status_t splash_display_open(splash_display* display, int devDir);
status_t splash_display_show(splash_display* display);
void splash_display_flush(splash_display* display);
	// Pushes what was drawn since splash_display_show() to the screen.
	// Needed for DRM drivers that scan out a shadow copy (virtio-gpu, ast,
	// mgag200, udl, ...); a no-op for the others and for fbdev.
void splash_display_drop_master(splash_display* display);
bool splash_display_lost(const splash_display* display, int devDir);
	// The device went away (simpledrm replaced by the real driver) or a
	// different device now sits at its node.
void splash_display_close(splash_display* display, bool handedOff);
	// Unmaps and closes. Unless handedOff, the VT goes back to KD_TEXT so a
	// boot that never reaches app_server shows its console.


#endif	/* VOS_SPLASH_DISPLAY_H */
