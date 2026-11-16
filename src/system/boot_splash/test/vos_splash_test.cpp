/*
 * Copyright 2026, Koan contributors. All rights reserved.
 * Distributed under the terms of the MIT License.
 */


// Host tests for vos-splash: no display needed, everything draws into a
// buffer in memory.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SupportDefs.h>

#define __BOOTSPLASH_KERNEL__
#include <splash_images.h>
	// dimensions and placement only
#include <boot/platform/generic/video_splash.h>
#include <boot_splash.h>

#include "splash_platform.h"
#include "splash_stages.h"


static int sFailures = 0;
static int sChecks = 0;

#define CHECK(condition) \
	do { \
		sChecks++; \
		if (!(condition)) { \
			sFailures++; \
			fprintf(stderr, "%s:%d: FAILED: %s\n", __FILE__, __LINE__, \
				#condition); \
		} \
	} while (0)


static void
test_stage_mapping()
{
	CHECK(vos_splash_stage_icon(1) == BOOT_SPLASH_STAGE_1_INIT_MODULES);
	CHECK(vos_splash_stage_icon(2) == BOOT_SPLASH_STAGE_2_BOOTSTRAP_FS);
	CHECK(vos_splash_stage_icon(3) == BOOT_SPLASH_STAGE_3_INIT_DEVICES);
	CHECK(vos_splash_stage_icon(4) == BOOT_SPLASH_STAGE_4_MOUNT_BOOT_FS);
	CHECK(vos_splash_stage_icon(5) == BOOT_SPLASH_STAGE_5_INIT_CPU_MODULES);
	CHECK(vos_splash_stage_icon(6) == BOOT_SPLASH_STAGE_6_INIT_VM_MODULES);
	CHECK(vos_splash_stage_icon(7) == BOOT_SPLASH_STAGE_7_RUN_BOOT_SCRIPT);
	CHECK(vos_splash_stage_icon(0) == -1);
	CHECK(vos_splash_stage_icon(8) == -1);
	CHECK(vos_splash_stage_icon(-1) == -1);

	CHECK(vos_splash_parse_stage("1") == 1);
	CHECK(vos_splash_parse_stage("7\n") == 7);
	CHECK(vos_splash_parse_stage("0") == -1);
	CHECK(vos_splash_parse_stage("8") == -1);
	CHECK(vos_splash_parse_stage("3x") == -1);
	CHECK(vos_splash_parse_stage("-3") == -1);
	CHECK(vos_splash_parse_stage("") == -1);
	CHECK(vos_splash_parse_stage("quit") == -1);
}


static void
test_image_decoding()
{
	uint16 width, height;

	// The air/OS artwork, data/artwork/boot_splash/airos_splash_*.png.
	video_splash_logo_size(&width, &height);
	CHECK(width == 240 && height == 263);
	size_t size = (size_t)width * height * 3;
	uint8* logo = (uint8*)malloc(size);
	CHECK(video_splash_decode_logo(logo, size) == B_OK);
	// The wrong size is refused instead of overrunning the buffer.
	CHECK(video_splash_decode_logo(logo, size - 3) == B_BAD_VALUE);
	CHECK(video_splash_decode_logo(logo, (size_t)width * height) == B_OK);
	free(logo);

	video_splash_icons_size(&width, &height);
	CHECK(width == 308 && height == 80);
	size = (size_t)width * height * 3;
	uint8* icons = (uint8*)malloc(size);
	CHECK(video_splash_decode_icons(icons, size) == B_OK);
	CHECK(video_splash_decode_icons(icons, (size_t)width * height) == B_OK);
	free(icons);

	CHECK(kSplashLogoWidth == 240 && kSplashLogoHeight == 263);
	CHECK(kSplashIconsWidth == 308 && kSplashIconsHeight == 80);
}


static uint32
pixel32(const splash_frame_buffer& fb, int x, int y)
{
	return *(const uint32*)(fb.frame_buffer + (size_t)y * fb.bytes_per_row
		+ (size_t)x * 4);
}


static uint32
brightness(uint32 pixel)
{
	return ((pixel >> 16) & 0xff) + ((pixel >> 8) & 0xff) + (pixel & 0xff);
}


// Sum of brightness over icon `icon` of the row.
static uint64
icon_brightness(const splash_frame_buffer& fb, int icon)
{
	int width, height, x, y;
	compute_splash_icons_placement(fb.width, fb.height, width, height, x, y);
	int left = x + width * icon / BOOT_SPLASH_STAGE_MAX;
	int right = x + width * (icon + 1) / BOOT_SPLASH_STAGE_MAX;

	uint64 sum = 0;
	for (int row = y; row < y + height; row++) {
		for (int column = left; column < right; column++)
			sum += brightness(pixel32(fb, column, row));
	}
	return sum;
}


static void
test_render_32()
{
	splash_frame_buffer fb;
	fb.width = 1024;
	fb.height = 768;
	fb.depth = 32;
	fb.bytes_per_row = 1024 * 4 + 64;
		// a stride wider than the mode, as dumb buffers may have
	fb.size = (size_t)fb.bytes_per_row * fb.height;
	fb.frame_buffer = (uint8*)malloc(fb.size);
	memset(fb.frame_buffer, 0xa5, fb.size);
		// the splash must clear this

	uint8* icons = NULL;
	CHECK(video_display_splash(fb, &icons) == B_OK);
	CHECK(icons != NULL);
	if (icons == NULL)
		return;

	CHECK(pixel32(fb, 0, 0) == 0);
	CHECK(pixel32(fb, fb.width - 1, fb.height - 1) == 0);

	// The logo is centred as on Haiku, and its centre is not background.
	int width, height, x, y;
	compute_splash_logo_placement(fb.width, fb.height, width, height, x, y);
	CHECK(x == (1024 - 240) / 2);
	CHECK(pixel32(fb, x + width / 2, y + height / 2) != 0);
	CHECK(pixel32(fb, x - 1, y + height / 2) == 0);

	// Before any stage every icon is the grayed out lower half.
	uint64 dim[BOOT_SPLASH_STAGE_MAX];
	for (int i = 0; i < BOOT_SPLASH_STAGE_MAX; i++) {
		dim[i] = icon_brightness(fb, i);
		CHECK(dim[i] > 0);
	}

	boot_splash_init(icons, &fb);
	boot_splash_set_stage(vos_splash_stage_icon(VOS_SPLASH_STAGE_INITRAMFS));
	boot_splash_set_stage(vos_splash_stage_icon(VOS_SPLASH_STAGE_ROOT_MOUNTED));

	// Lit icons are brighter than they were, unlit ones unchanged.
	CHECK(icon_brightness(fb, 0) > dim[0]);
	CHECK(icon_brightness(fb, 1) > dim[1]);
	for (int i = 2; i < BOOT_SPLASH_STAGE_MAX; i++)
		CHECK(icon_brightness(fb, i) == dim[i]);

	// A lit pixel is the upper half of the icon image, an unlit one the
	// lower half (BGR source, XRGB8888 frame buffer).
	compute_splash_icons_placement(fb.width, fb.height, width, height, x, y);
	int litX = width * 0 / BOOT_SPLASH_STAGE_MAX + 20;
	int unlitX = width * 4 / BOOT_SPLASH_STAGE_MAX + 20;
	int row = 20;
	const uint8* upper = icons + ((size_t)row * kSplashIconsWidth + litX) * 3;
	const uint8* lower = icons + ((size_t)(row + kSplashIconsHeight / 2)
		* kSplashIconsWidth + unlitX) * 3;
	CHECK(pixel32(fb, x + litX, y + row)
		== (uint32)((upper[2] << 16) | (upper[1] << 8) | upper[0]));
	CHECK(pixel32(fb, x + unlitX, y + row)
		== (uint32)((lower[2] << 16) | (lower[1] << 8) | lower[0]));

	// Out of range stages do nothing.
	boot_splash_set_stage(-1);
	boot_splash_set_stage(BOOT_SPLASH_STAGE_MAX);
	CHECK(icon_brightness(fb, 6) == dim[6]);

	boot_splash_set_stage(vos_splash_stage_icon(VOS_SPLASH_STAGE_APP_SERVER));
	CHECK(icon_brightness(fb, 6) > dim[6]);

	boot_splash_uninit();
	free(icons);
	free(fb.frame_buffer);
}


static void
test_render_16_and_small()
{
	// RGB565, as fbdev offers on some boards.
	splash_frame_buffer fb;
	fb.width = 800;
	fb.height = 600;
	fb.depth = 16;
	fb.bytes_per_row = 800 * 2;
	fb.size = (size_t)fb.bytes_per_row * fb.height;
	fb.frame_buffer = (uint8*)calloc(1, fb.size);

	uint8* icons = NULL;
	CHECK(video_display_splash(fb, &icons) == B_OK);
	int width, height, x, y;
	compute_splash_logo_placement(fb.width, fb.height, width, height, x, y);
	CHECK(*(uint16*)(fb.frame_buffer + (size_t)(y + height / 2)
		* fb.bytes_per_row + (size_t)(x + width / 2) * 2) != 0);
	free(icons);
	free(fb.frame_buffer);

	// A screen smaller than logo and icons: nothing is written past the
	// buffer (the guard band would change).
	const size_t guard = 4096;
	fb.width = 200;
	fb.height = 150;
	fb.depth = 32;
	fb.bytes_per_row = 200 * 4;
	fb.size = (size_t)fb.bytes_per_row * fb.height;
	fb.frame_buffer = (uint8*)malloc(fb.size + guard);
	memset(fb.frame_buffer + fb.size, 0x5a, guard);
	icons = NULL;
	CHECK(video_display_splash(fb, &icons) == B_OK);
	boot_splash_init(icons, &fb);
	for (int stage = 1; stage <= VOS_SPLASH_STAGE_COUNT; stage++)
		boot_splash_set_stage(vos_splash_stage_icon(stage));
	bool intact = true;
	for (size_t i = 0; i < guard; i++)
		intact &= fb.frame_buffer[fb.size + i] == 0x5a;
	CHECK(intact);
	boot_splash_uninit();
	free(icons);
	free(fb.frame_buffer);
}


int
main()
{
	test_stage_mapping();
	test_image_decoding();
	test_render_32();
	test_render_16_and_small();

	printf("vos_splash_test: %d checks, %d failed\n", sChecks, sFailures);
	return sFailures == 0 ? 0 : 1;
}
