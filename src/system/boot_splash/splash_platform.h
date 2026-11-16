/*
 * Copyright 2026, Koan contributors. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef VOS_SPLASH_PLATFORM_H
#define VOS_SPLASH_PLATFORM_H


// The Linux platform layer of the boot splash. On Haiku the boot loader
// described the frame buffer in kernel_args and handed the decompressed
// icons to the kernel as a boot item; here one process owns a mapped
// frame buffer (a DRM dumb buffer, or fbdev) and passes it along directly.

#include <SupportDefs.h>


struct splash_frame_buffer {
	uint8*	frame_buffer;
	uint32	width;
	uint32	height;
	int32	depth;
		// bits per pixel as the blitter knows them: 8, 15, 16, 24, 30, 32
	uint32	bytes_per_row;
	size_t	size;
		// mapped size; never write beyond it
};


// video_splash.cpp (was the boot loader's generic video splash)
status_t video_display_splash(const splash_frame_buffer& frameBuffer,
	uint8** _icons);
	// Clears the screen, draws the logo and the grayed out icon row.
	// On success *_icons holds the decompressed icon image (both halves;
	// free() it), which boot_splash_init() takes.
status_t video_uncompress_image(const uint8* compressed,
	unsigned int compressedSize, uint8* uncompressed,
	unsigned int uncompressedSize);
void video_blit_image(const splash_frame_buffer& frameBuffer,
	const uint8* data, uint16 width, uint16 height, uint16 imageWidth,
	uint16 left, uint16 top);

// Image accessors, so code outside video_splash.cpp (the stage layer and the
// tests) does not need its own copy of the compressed arrays.
void video_splash_logo_size(uint16* width, uint16* height);
void video_splash_icons_size(uint16* width, uint16* height);
status_t video_splash_decode_logo(uint8* buffer, size_t size);
status_t video_splash_decode_icons(uint8* buffer, size_t size);

// boot_splash.cpp (was the kernel's boot splash) is declared in
// <boot_splash.h>.

// Set by the display layer for 8 bit modes; NULL where no palette exists.
extern void (*video_set_palette)(const uint8* palette);


#endif	/* VOS_SPLASH_PLATFORM_H */
