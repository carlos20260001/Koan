/*
 * Copyright 2008-2009, Haiku.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 *		Stefano Ceccherini <stefano.ceccherini@gmail.com>
 *		Axel Dörfler, axeld@pinc-software.de
 */


#include "DirectWindowInfo.h"

#include <math.h>
#include <stdio.h>
#include <string.h>
#include <syslog.h>

#include <Autolock.h>
#include <stdint.h>

// V's app_server has no upstream <kernel.h>. User-space addresses have the
// top bit clear on every architecture V targets; kernel addresses never
// reach this user-space server.
#ifndef IS_USER_ADDRESS
#define IS_USER_ADDRESS(address) \
	((uintptr_t)(address) < ((uintptr_t)1 << (sizeof(uintptr_t) * 8 - 1)))
#endif

#include "RenderingBuffer.h"
#include "clipping.h"


DirectWindowInfo::DirectWindowInfo()
	:
	fBufferInfo(NULL),
	fBufferArea(-1),
	fOriginalFeel(B_NORMAL_WINDOW_FEEL),
	fFullScreen(false)
{
	fBufferArea = create_area("direct area", (void**)&fBufferInfo,
		B_ANY_ADDRESS, DIRECT_BUFFER_INFO_AREA_SIZE,
		B_NO_LOCK, B_READ_AREA | B_WRITE_AREA | B_CLONEABLE_AREA);

	memset(fBufferInfo, 0, DIRECT_BUFFER_INFO_AREA_SIZE);
	fBufferInfo->buffer_state = B_DIRECT_STOP;
	fBufferInfo->bits_area = -1;
	fBufferInfo->drawing_bits_area = -1;

	// The sync words live in the shared area tail, mapped by the daemon too.
	fDisableEvent.SetTo(direct_window_disable_state(fBufferInfo));
	fAcknowledgeEvent.SetTo(direct_window_ack_state(fBufferInfo));

	// We hold the first liveness state, the client daemon watches it. We
	// watch the client's while waiting for the acknowledge.
	shared_liveness_init(direct_window_server_liveness(fBufferInfo));
	shared_liveness_init(direct_window_client_liveness(fBufferInfo));
	shared_liveness_hold(direct_window_server_liveness(fBufferInfo));
	fAcknowledgeEvent.SetPeer(direct_window_client_liveness(fBufferInfo));
}


DirectWindowInfo::~DirectWindowInfo()
{
	// The client may be blocked on either word, or about to be.
	fDisableEvent.MarkDead();
	fAcknowledgeEvent.MarkDead();
	shared_liveness_release(direct_window_server_liveness(fBufferInfo));

	// this should make the client die in case it's still running
	fBufferInfo->bits = NULL;
	fBufferInfo->bytes_per_row = 0;

	delete_area(fBufferArea);
}


status_t
DirectWindowInfo::InitCheck() const
{
	if (fBufferArea < B_OK)
		return fBufferArea;

	return B_OK;
}


status_t
DirectWindowInfo::GetSyncData(direct_window_sync_data& data) const
{
	data.area = fBufferArea;

	return B_OK;
}


/*!	A rectangle in the window system's coordinates in frame buffer pixels,
	rounded the way the Painter rounds (DrawingEngine::_ScaleRegion()), so
	that what a direct window draws meets what app_server draws next to it.
*/
static clipping_rect
to_device_pixels(clipping_rect rect, float scale)
{
	rect.left = (int32)floorf(rect.left * scale);
	rect.top = (int32)floorf(rect.top * scale);
	rect.right = (int32)floorf((rect.right + 1) * scale) - 1;
	rect.bottom = (int32)floorf((rect.bottom + 1) * scale) - 1;
	return rect;
}


/*!	The area that holds \a buffer, made cloneable, if the buffer is an area
	of its own; -1 otherwise. A heap allocation is never handed out: the
	area around it would hold other things too.
*/
static area_id
area_of_buffer(RenderingBuffer* buffer)
{
	if (buffer == NULL || !IS_USER_ADDRESS(buffer->Bits()))
		return -1;

	area_id area = area_for(buffer->Bits());
	area_info info;
	if (area < 0 || get_area_info(area, &info) != B_OK
		|| info.address != buffer->Bits()
		|| info.size < (size_t)buffer->BytesPerRow() * buffer->Height())
		return -1;

	set_area_protection(area, B_READ_AREA | B_WRITE_AREA | B_CLONEABLE_AREA);
	return area;
}


status_t
DirectWindowInfo::SetState(direct_buffer_state bufferState,
	direct_driver_state driverState, RenderingBuffer* buffer,
	const BRect& windowFrame, const BRegion& clipRegion, uint16 deviceScale,
	RenderingBuffer* drawingBuffer)
{
	if ((fBufferInfo->buffer_state & B_DIRECT_MODE_MASK) == B_DIRECT_STOP
		&& (bufferState & B_DIRECT_MODE_MASK) != B_DIRECT_START)
		return B_OK;

	// A NULL buffer means the screen had none to hand out (teardown
	// race); never dereference it, just stop the direct client.
	if (buffer == NULL
			&& (bufferState & B_DIRECT_MODE_MASK) != B_DIRECT_STOP) {
		syslog(LOG_ERR, "DirectWindowInfo::SetState() called with a "
			"NULL buffer; stopping the direct window\n");
		fBufferInfo->buffer_state = (direct_buffer_state)
			((bufferState & ~B_DIRECT_MODE_MASK) | B_DIRECT_STOP);
		return _SynchronizeWithClient();
	}

	fBufferInfo->buffer_state = bufferState;

	if ((int)driverState != -1)
		fBufferInfo->driver_state = driverState;

	if ((bufferState & B_BUFFER_RESET) != 0 || fBufferInfo->bits_area < 0) {
		void* bits = buffer->Bits();
		if (IS_USER_ADDRESS(bits)) {
			fBufferInfo->bits = NULL;

			area_id area = area_for(bits);
			fBufferInfo->bits_area = area;

			// make sure the area is cloneable
			set_area_protection(area, B_READ_AREA | B_WRITE_AREA | B_CLONEABLE_AREA);
		} else {
			// framebuffer is in kernel address space
			// TODO: update all drivers and then drop this case!
			fBufferInfo->bits = bits;
		}
		fBufferInfo->bytes_per_row = buffer->BytesPerRow();

		switch (buffer->ColorSpace()) {
			case B_RGBA64:
			case B_RGBA64_BIG:
				fBufferInfo->bits_per_pixel = 64;
				break;
			case B_RGB48:
			case B_RGB48_BIG:
				fBufferInfo->bits_per_pixel = 48;
				break;
			case B_RGB32:
			case B_RGBA32:
			case B_RGB32_BIG:
			case B_RGBA32_BIG:
				fBufferInfo->bits_per_pixel = 32;
				break;
			case B_RGB24:
			case B_RGB24_BIG:
				fBufferInfo->bits_per_pixel = 24;
				break;
			case B_RGB16:
			case B_RGB16_BIG:
			case B_RGB15:
			case B_RGB15_BIG:
				fBufferInfo->bits_per_pixel = 16;
				break;
			case B_CMAP8:
			case B_GRAY8:
				fBufferInfo->bits_per_pixel = 8;
				break;
			default:
				syslog(LOG_ERR,
					"unknown colorspace in DirectWindowInfo::SetState()!\n");
				fBufferInfo->bits_per_pixel = 0;
				break;
		}

		fBufferInfo->pixel_format = buffer->ColorSpace();
		fBufferInfo->layout = B_BUFFER_NONINTERLEAVED;
		fBufferInfo->orientation = B_BUFFER_TOP_TO_BOTTOM;
			// TODO

		fBufferInfo->drawing_bits_area = area_of_buffer(drawingBuffer);
		fBufferInfo->drawing_bytes_per_row
			= fBufferInfo->drawing_bits_area >= 0
				? drawingBuffer->BytesPerRow() : 0;
	}

	// Only windows that draw in frame buffer pixels are told the density;
	// for the others the coordinates are their own and it stays 0.
	fBufferInfo->device_scale = deviceScale;
	float scale = deviceScale > 100 ? deviceScale / 100.0f : 1;

	if ((bufferState & B_DIRECT_MODE_MASK) != B_DIRECT_STOP) {
		fBufferInfo->window_bounds = to_clipping_rect(windowFrame);

		const int32 kMaxClipRectsCount = (DIRECT_BUFFER_INFO_AREA_SIZE
			- sizeof(direct_buffer_info) - DIRECT_WINDOW_SYNC_RESERVED)
			/ sizeof(clipping_rect);

		fBufferInfo->clip_list_count = min_c(clipRegion.CountRects(),
			kMaxClipRectsCount);
		fBufferInfo->clip_bounds = clipRegion.FrameInt();

		for (uint32 i = 0; i < fBufferInfo->clip_list_count; i++)
			fBufferInfo->clip_list[i] = clipRegion.RectAtInt(i);

		if (scale != 1) {
			fBufferInfo->window_bounds = to_device_pixels(
				fBufferInfo->window_bounds, scale);
			fBufferInfo->clip_bounds = to_device_pixels(
				fBufferInfo->clip_bounds, scale);
			for (uint32 i = 0; i < fBufferInfo->clip_list_count; i++) {
				fBufferInfo->clip_list[i] = to_device_pixels(
					fBufferInfo->clip_list[i], scale);
			}
		}
	}

	return _SynchronizeWithClient();
}


void
DirectWindowInfo::GetClipping(BRegion& region) const
{
	region.MakeEmpty();
	if (fBufferInfo == NULL
		|| (fBufferInfo->buffer_state & B_DIRECT_MODE_MASK) == B_DIRECT_STOP)
		return;
	for (uint32 i = 0; i < fBufferInfo->clip_list_count; i++)
		region.Include(fBufferInfo->clip_list[i]);
}


void
DirectWindowInfo::EnableFullScreen(const BRect& frame, window_feel feel)
{
	fOriginalFrame = frame;
	fOriginalFeel = feel;
	fFullScreen = true;
}


void
DirectWindowInfo::DisableFullScreen()
{
	fFullScreen = false;
}


status_t
DirectWindowInfo::_SynchronizeWithClient()
{
	// Posting wakes the client daemon into DirectConnected(). Wait up to
	// half a second for it to return.
	fDisableEvent.Post();

	status_t status;
	do {
		status = fAcknowledgeEvent.WaitWithTimeout(500000);
	} while (status == B_INTERRUPTED);

	return status;
}
