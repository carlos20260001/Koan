/*
 * Copyright 2026, Koan contributors. All rights reserved.
 * Distributed under the terms of the MIT License.
 */


// The Linux replacement for the boot loader's frame buffer: a DRM dumb
// buffer (the technique of VitruvianOS app_server's linux/drm/modeset.c,
// with raw ioctls so the program needs no libdrm and links statically),
// or the fbdev frame buffer where no DRM driver is bound (nomodeset).

#include "splash_display.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include <drm/drm.h>
#include <drm/drm_mode.h>
#include <linux/fb.h>
#include <linux/kd.h>


static_assert(sizeof(drm_mode_modeinfo) == sizeof(((splash_display*)0)->mode),
	"splash_display::mode must hold a drm_mode_modeinfo");

static const int kMaxCards = 8;
static int sFbDev = -1;
	// for the 8 bit palette callback


static int
xioctl(int fd, unsigned long request, void* arg)
{
	int result;
	do {
		result = ioctl(fd, request, arg);
	} while (result == -1 && (errno == EINTR || errno == EAGAIN));
	return result;
}


static void
reset(splash_display* display)
{
	memset(display, 0, sizeof(*display));
	display->kind = SPLASH_DISPLAY_NONE;
	display->fd = -1;
	display->tty = -1;
}


static uint32
present_cards(int devDir)
{
	uint32 present = 0;
	for (int i = 0; i < kMaxCards; i++) {
		char name[32];
		snprintf(name, sizeof(name), "dri/card%d", i);
		struct stat st;
		if (fstatat(devDir, name, &st, 0) == 0 && S_ISCHR(st.st_mode))
			present |= 1u << i;
	}
	return present;
}


static uint32 sCardsAtFbOpen;


// #pragma mark - DRM


static bool
drm_connector_mode(int fd, uint32 connectorId, drm_mode_modeinfo* mode,
	uint32* encoderId, uint32** encoders, uint32* encoderCount)
{
	drm_mode_get_connector conn;
	drm_mode_modeinfo* modes = NULL;
	uint32* encoderIds = NULL;

	// Counts change while the connector is probed: retry until stable.
	for (int attempt = 0; attempt < 4; attempt++) {
		memset(&conn, 0, sizeof(conn));
		conn.connector_id = connectorId;
		if (xioctl(fd, DRM_IOCTL_MODE_GETCONNECTOR, &conn) != 0)
			return false;
		if (conn.connection != 1 || conn.count_modes == 0)
			return false;

		uint32 modeCount = conn.count_modes;
		uint32 encCount = conn.count_encoders;
		free(modes);
		free(encoderIds);
		modes = (drm_mode_modeinfo*)calloc(modeCount, sizeof(*modes));
		encoderIds = (uint32*)calloc(encCount + 1, sizeof(uint32));
		if (modes == NULL || encoderIds == NULL)
			break;

		memset(&conn, 0, sizeof(conn));
		conn.connector_id = connectorId;
		conn.count_modes = modeCount;
		conn.modes_ptr = (uintptr_t)modes;
		conn.count_encoders = encCount;
		conn.encoders_ptr = (uintptr_t)encoderIds;
		if (xioctl(fd, DRM_IOCTL_MODE_GETCONNECTOR, &conn) != 0)
			break;
		if (conn.count_modes != modeCount || conn.count_encoders != encCount)
			continue;
		if (conn.connection != 1 || modeCount == 0)
			break;

		uint32 chosen = 0;
		for (uint32 i = 0; i < modeCount; i++) {
			if ((modes[i].type & DRM_MODE_TYPE_PREFERRED) != 0) {
				chosen = i;
				break;
			}
		}
		*mode = modes[chosen];
		*encoderId = conn.encoder_id;
		*encoders = encoderIds;
		*encoderCount = encCount;
		free(modes);
		return true;
	}

	free(modes);
	free(encoderIds);
	return false;
}


static uint32
drm_find_crtc(int fd, uint32 encoderId, const uint32* encoders,
	uint32 encoderCount, const uint32* crtcs, uint32 crtcCount)
{
	// Keep the CRTC the firmware or fbcon already drives: no mode change.
	if (encoderId != 0) {
		drm_mode_get_encoder enc;
		memset(&enc, 0, sizeof(enc));
		enc.encoder_id = encoderId;
		if (xioctl(fd, DRM_IOCTL_MODE_GETENCODER, &enc) == 0
			&& enc.crtc_id != 0)
			return enc.crtc_id;
	}

	for (uint32 i = 0; i < encoderCount; i++) {
		drm_mode_get_encoder enc;
		memset(&enc, 0, sizeof(enc));
		enc.encoder_id = encoders[i];
		if (xioctl(fd, DRM_IOCTL_MODE_GETENCODER, &enc) != 0)
			continue;
		for (uint32 j = 0; j < crtcCount && j < 32; j++) {
			if ((enc.possible_crtcs & (1u << j)) != 0)
				return crtcs[j];
		}
	}
	return 0;
}


static status_t
drm_open_card(splash_display* display, int devDir, int index)
{
	snprintf(display->name, sizeof(display->name), "dri/card%d", index);
	int fd = openat(devDir, display->name, O_RDWR | O_CLOEXEC);
	if (fd < 0)
		return B_ENTRY_NOT_FOUND;
	display->fd = fd;

	struct stat st;
	if (fstat(fd, &st) == 0)
		display->device = st.st_rdev;

	drm_get_cap cap;
	memset(&cap, 0, sizeof(cap));
	cap.capability = DRM_CAP_DUMB_BUFFER;
	if (xioctl(fd, DRM_IOCTL_GET_CAP, &cap) != 0 || cap.value == 0)
		return B_NOT_SUPPORTED;

	// The first opener is master already; this fails when someone else
	// (a running app_server) holds the card, and then it is not ours.
	if (xioctl(fd, DRM_IOCTL_SET_MASTER, NULL) != 0)
		return B_NOT_ALLOWED;
	display->master = true;

	uint32* connectors = NULL;
	uint32* crtcs = NULL;
	drm_mode_card_res res;
	status_t status = B_ERROR;
	for (int attempt = 0; attempt < 4; attempt++) {
		memset(&res, 0, sizeof(res));
		if (xioctl(fd, DRM_IOCTL_MODE_GETRESOURCES, &res) != 0)
			return B_ERROR;
		uint32 connectorCount = res.count_connectors;
		uint32 crtcCount = res.count_crtcs;
		if (connectorCount == 0 || crtcCount == 0)
			return B_ENTRY_NOT_FOUND;

		free(connectors);
		free(crtcs);
		connectors = (uint32*)calloc(connectorCount, sizeof(uint32));
		crtcs = (uint32*)calloc(crtcCount, sizeof(uint32));
		if (connectors == NULL || crtcs == NULL) {
			status = B_NO_MEMORY;
			break;
		}

		memset(&res, 0, sizeof(res));
		res.count_connectors = connectorCount;
		res.connector_id_ptr = (uintptr_t)connectors;
		res.count_crtcs = crtcCount;
		res.crtc_id_ptr = (uintptr_t)crtcs;
		if (xioctl(fd, DRM_IOCTL_MODE_GETRESOURCES, &res) != 0)
			break;
		if (res.count_connectors != connectorCount
			|| res.count_crtcs != crtcCount)
			continue;

		status = B_ENTRY_NOT_FOUND;
		for (uint32 i = 0; i < connectorCount; i++) {
			drm_mode_modeinfo mode;
			uint32 encoderId, encoderCount;
			uint32* encoders;
			if (!drm_connector_mode(fd, connectors[i], &mode, &encoderId,
					&encoders, &encoderCount))
				continue;
			uint32 crtc = drm_find_crtc(fd, encoderId, encoders,
				encoderCount, crtcs, crtcCount);
			free(encoders);
			if (crtc == 0)
				continue;

			display->connector = connectors[i];
			display->crtc = crtc;
			memcpy(display->mode, &mode, sizeof(mode));
			status = B_OK;
			break;
		}
		break;
	}
	free(connectors);
	free(crtcs);
	if (status != B_OK)
		return status;

	const drm_mode_modeinfo* mode = (const drm_mode_modeinfo*)display->mode;

	drm_mode_create_dumb create;
	memset(&create, 0, sizeof(create));
	create.width = mode->hdisplay;
	create.height = mode->vdisplay;
	create.bpp = 32;
	if (xioctl(fd, DRM_IOCTL_MODE_CREATE_DUMB, &create) != 0)
		return B_NO_MEMORY;
	display->handle = create.handle;

	drm_mode_fb_cmd fbCommand;
	memset(&fbCommand, 0, sizeof(fbCommand));
	fbCommand.width = mode->hdisplay;
	fbCommand.height = mode->vdisplay;
	fbCommand.pitch = create.pitch;
	fbCommand.bpp = 32;
	fbCommand.depth = 24;
		// XRGB8888, which is what blit32() writes
	fbCommand.handle = create.handle;
	if (xioctl(fd, DRM_IOCTL_MODE_ADDFB, &fbCommand) != 0)
		return B_ERROR;
	display->fbId = fbCommand.fb_id;

	drm_mode_map_dumb mapDumb;
	memset(&mapDumb, 0, sizeof(mapDumb));
	mapDumb.handle = create.handle;
	if (xioctl(fd, DRM_IOCTL_MODE_MAP_DUMB, &mapDumb) != 0)
		return B_ERROR;

	void* map = mmap(NULL, create.size, PROT_READ | PROT_WRITE, MAP_SHARED,
		fd, mapDumb.offset);
	if (map == MAP_FAILED)
		return B_NO_MEMORY;

	display->map = map;
	display->mapSize = create.size;
	display->frame_buffer.frame_buffer = (uint8*)map;
	display->frame_buffer.width = mode->hdisplay;
	display->frame_buffer.height = mode->vdisplay;
	display->frame_buffer.depth = 32;
	display->frame_buffer.bytes_per_row = create.pitch;
	display->frame_buffer.size = create.size;
	display->kind = SPLASH_DISPLAY_DRM;
	return B_OK;
}


// #pragma mark - fbdev


static void
fbdev_set_palette(const uint8* palette)
{
	if (sFbDev < 0)
		return;

	uint16 red[256], green[256], blue[256];
	for (int i = 0; i < 256; i++) {
		red[i] = palette[i * 3 + 0] << 8;
		green[i] = palette[i * 3 + 1] << 8;
		blue[i] = palette[i * 3 + 2] << 8;
	}
	fb_cmap cmap;
	memset(&cmap, 0, sizeof(cmap));
	cmap.start = 0;
	cmap.len = 256;
	cmap.red = red;
	cmap.green = green;
	cmap.blue = blue;
	xioctl(sFbDev, FBIOPUTCMAP, &cmap);
}


static status_t
fbdev_open(splash_display* display, int devDir)
{
	strcpy(display->name, "fb0");
	int fd = openat(devDir, display->name, O_RDWR | O_CLOEXEC);
	if (fd < 0)
		return B_ENTRY_NOT_FOUND;
	display->fd = fd;

	struct stat st;
	if (fstat(fd, &st) == 0)
		display->device = st.st_rdev;

	fb_fix_screeninfo fix;
	fb_var_screeninfo var;
	if (xioctl(fd, FBIOGET_FSCREENINFO, &fix) != 0
		|| xioctl(fd, FBIOGET_VSCREENINFO, &var) != 0)
		return B_ERROR;
	if (fix.type != FB_TYPE_PACKED_PIXELS)
		return B_NOT_SUPPORTED;

	int32 depth;
	switch (var.bits_per_pixel) {
		case 8:
			depth = 8;
			break;
		case 16:
			depth = var.green.length == 5 ? 15 : 16;
			break;
		case 24:
			depth = 24;
			break;
		case 32:
			depth = var.red.length == 10 ? 30 : 32;
			break;
		default:
			return B_NOT_SUPPORTED;
	}

	void* map = mmap(NULL, fix.smem_len, PROT_READ | PROT_WRITE, MAP_SHARED,
		fd, 0);
	if (map == MAP_FAILED)
		return B_NO_MEMORY;
	display->map = map;
	display->mapSize = fix.smem_len;

	// Draw where the panned view is.
	size_t offset = (size_t)var.yoffset * fix.line_length
		+ (size_t)var.xoffset * var.bits_per_pixel / 8;
	size_t visible = (size_t)fix.line_length * var.yres;
	if (offset + visible > fix.smem_len)
		offset = 0;
	if (visible > fix.smem_len)
		return B_BAD_VALUE;

	display->frame_buffer.frame_buffer = (uint8*)map + offset;
	display->frame_buffer.width = var.xres;
	display->frame_buffer.height = var.yres;
	display->frame_buffer.depth = depth;
	display->frame_buffer.bytes_per_row = fix.line_length;
	display->frame_buffer.size = fix.smem_len - offset;
	display->kind = SPLASH_DISPLAY_FBDEV;

	if (depth == 8) {
		sFbDev = fd;
		video_set_palette = fbdev_set_palette;
	}
	sCardsAtFbOpen = present_cards(devDir);
	return B_OK;
}


// #pragma mark - public


static void
open_tty(splash_display* display, int devDir)
{
	display->tty = openat(devDir, "tty0", O_RDWR | O_CLOEXEC | O_NOCTTY);
}


status_t
splash_display_open(splash_display* display, int devDir)
{
	reset(display);

	for (int i = 0; i < kMaxCards; i++) {
		if (drm_open_card(display, devDir, i) == B_OK) {
			open_tty(display, devDir);
			return B_OK;
		}
		splash_display_close(display, true);
	}

	if (fbdev_open(display, devDir) == B_OK) {
		open_tty(display, devDir);
		return B_OK;
	}
	splash_display_close(display, true);
	return B_ENTRY_NOT_FOUND;
}


status_t
splash_display_show(splash_display* display)
{
	// Keep fbcon from drawing its cursor and messages over the splash.
	// Best effort: without a VT the splash still shows.
	if (display->tty >= 0
		&& xioctl(display->tty, KDSETMODE, (void*)(uintptr_t)KD_GRAPHICS)
			!= 0) {
		close(display->tty);
		display->tty = -1;
	}

	if (display->kind == SPLASH_DISPLAY_DRM) {
		drm_mode_crtc crtc;
		memset(&crtc, 0, sizeof(crtc));
		crtc.crtc_id = display->crtc;
		crtc.fb_id = display->fbId;
		crtc.set_connectors_ptr = (uintptr_t)&display->connector;
		crtc.count_connectors = 1;
		memcpy(&crtc.mode, display->mode, sizeof(crtc.mode));
		crtc.mode_valid = 1;
		if (xioctl(display->fd, DRM_IOCTL_MODE_SETCRTC, &crtc) != 0)
			return B_ERROR;
		return B_OK;
	}

	if (display->kind == SPLASH_DISPLAY_FBDEV) {
		// Unblank in case the console blanked the screen.
		xioctl(display->fd, FBIOBLANK, (void*)(uintptr_t)FB_BLANK_UNBLANK);
		return B_OK;
	}

	return B_NO_INIT;
}


void
splash_display_flush(splash_display* display)
{
	if (display->kind != SPLASH_DISPLAY_DRM || !display->master)
		return;

	// No clips: the whole frame buffer. Drivers that scan out the buffer
	// directly answer ENOSYS, which is fine.
	drm_mode_fb_dirty_cmd dirty;
	memset(&dirty, 0, sizeof(dirty));
	dirty.fb_id = display->fbId;
	ioctl(display->fd, DRM_IOCTL_MODE_DIRTYFB, &dirty);
}


void
splash_display_drop_master(splash_display* display)
{
	if (display->kind != SPLASH_DISPLAY_DRM || !display->master)
		return;

	// The frame buffer stays mapped and on screen; only the right to
	// modeset goes, so app_server's drmSetMaster() (or logind's
	// TakeDevice) succeeds while this process still keeps the picture.
	if (xioctl(display->fd, DRM_IOCTL_DROP_MASTER, NULL) == 0)
		display->master = false;
	else
		fprintf(stderr, "vos-splash: DRM_IOCTL_DROP_MASTER: %s\n",
			strerror(errno));
}


bool
splash_display_lost(const splash_display* display, int devDir)
{
	struct stat st;

	switch (display->kind) {
		case SPLASH_DISPLAY_DRM:
		{
			drm_mode_card_res res;
			memset(&res, 0, sizeof(res));
			if (ioctl(display->fd, DRM_IOCTL_MODE_GETRESOURCES, &res) != 0
				&& errno == ENODEV)
				return true;
			if (fstatat(devDir, display->name, &st, 0) != 0
				|| st.st_rdev != display->device)
				return true;
			return false;
		}

		case SPLASH_DISPLAY_FBDEV:
		{
			fb_var_screeninfo var;
			if (ioctl(display->fd, FBIOGET_VSCREENINFO, &var) != 0)
				return true;
			if (var.xres != display->frame_buffer.width
				|| var.yres != display->frame_buffer.height)
				return true;
			// A DRM driver came up and took the hardware from efifb/vesafb.
			return (present_cards(devDir) & ~sCardsAtFbOpen) != 0;
		}

		default:
			// Nothing to draw on yet: a device may have appeared.
			return present_cards(devDir) != 0
				|| fstatat(devDir, "fb0", &st, 0) == 0;
	}
}


void
splash_display_close(splash_display* display, bool handedOff)
{
	if (display->map != NULL && display->map != MAP_FAILED)
		munmap(display->map, display->mapSize);

	if (display->tty >= 0) {
		if (!handedOff)
			xioctl(display->tty, KDSETMODE, (void*)(uintptr_t)KD_TEXT);
		close(display->tty);
	}

	if (display->fd >= 0) {
		if (display->fd == sFbDev) {
			sFbDev = -1;
			video_set_palette = NULL;
		}
		// Closing releases the dumb buffer and, if this was the last
		// opener, lets the kernel's fbdev emulation take the CRTC back.
		close(display->fd);
	}

	reset(display);
}
