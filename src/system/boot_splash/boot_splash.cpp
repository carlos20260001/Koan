/*
 * Copyright 2008-2010, Haiku, Inc. All Rights Reserved.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 *		Artur Wyszynski <harakash@gmail.com>
 */


// Linux port (vos-splash): the frame buffer comes from splash_display.cpp
// instead of the FRAME_BUFFER_BOOT_INFO boot item, and there is no kernel
// debug screen to defer to; the stage drawing below is the kernel's.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define __BOOTSPLASH_KERNEL__
#include <splash_images.h>
#include <boot/platform/generic/video_blitter.h>
#include <boot/platform/generic/video_splash.h>

#include "splash_platform.h"

#include <boot_splash.h>


//#define TRACE_BOOT_SPLASH 1
#ifdef TRACE_BOOT_SPLASH
#	define TRACE(x...) printf(x);
#else
#	define TRACE(x...) ;
#endif


static const struct splash_frame_buffer *sInfo;
static uint8 *sUncompressedIcons;


//	#pragma mark - exported functions


void
boot_splash_init(uint8 *bootSplash, const struct splash_frame_buffer *info)
{
	TRACE("boot_splash_init: enter\n");

	// A loader that showed no splash screen has no icons for us either.
	if (bootSplash == NULL || info == NULL)
		return;

	sInfo = info;

	sUncompressedIcons = bootSplash;
}


void
boot_splash_uninit(void)
{
	sInfo = NULL;
}


void
boot_splash_set_stage(int stage)
{
	TRACE("boot_splash_set_stage: stage=%d\n", stage);

	if (sInfo == NULL || stage < 0 || stage >= BOOT_SPLASH_STAGE_MAX)
		return;

	int width, height, x, y;
	compute_splash_icons_placement(sInfo->width, sInfo->height,
		width, height, x, y);

	// Linux port: the boot loader never drew on a screen too small for the
	// icon row; a tiny fbdev mode must not make this write past the buffer.
	if (x < 0 || y < 0 || (uint32)(x + width) > sInfo->width
		|| (uint32)(y + height) > sInfo->height)
		return;

	int stageLeftEdge = width * stage / BOOT_SPLASH_STAGE_MAX;
	int stageRightEdge = width * (stage + 1) / BOOT_SPLASH_STAGE_MAX;

	BlitParameters params;
	params.from = sUncompressedIcons;
	params.fromWidth = kSplashIconsWidth;
	params.fromLeft = stageLeftEdge;
	params.fromTop = 0;
	params.fromRight = stageRightEdge;
	params.fromBottom = height;
	params.to = sInfo->frame_buffer;
	params.toBytesPerRow = sInfo->bytes_per_row;
	params.toLeft = stageLeftEdge + x;
	params.toTop = y;

	blit(params, sInfo->depth);
}
