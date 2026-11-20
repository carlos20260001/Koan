/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Copyright 2005-2012, Haiku.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 *		Stephan Aßmus <superstippi@gmx.de>
 */


#include "HWInterface.h"

#include <Autolock.h>
#include <Entry.h>
#include <FindDirectory.h>
#include <Path.h>

#include <new>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <vesa/vesa_info.h>

#include "drawing_support.h"
#include "MallocBuffer.h"

#include "DrawingEngine.h"
#include "GlobalSubpixelSettings.h"
#include "RenderingBuffer.h"
#include "SystemPalette.h"


using std::nothrow;


HWInterfaceListener::HWInterfaceListener()
{
}


HWInterfaceListener::~HWInterfaceListener()
{
}


// #pragma mark - HWInterface


HWInterface::HWInterface()
	:
	MultiLocker("hw interface lock"),
	fFloatingOverlaysLock("floating overlays lock"),
	fStaleLock("stale drawing buffer"),
	fHasStale(0),
	fCursor(NULL),
	fDragBitmap(NULL),
	fDragBitmapOffset(0, 0),
	fCursorAndDragBitmap(NULL),
	fCursorVisible(false),
	fSoftwareScale(100),
	fRenderScale(100),
	fLogicalWidth(0),
	fLogicalHeight(0),
	fCursorObscured(false),
	fHardwareCursorEnabled(false),
	fCursorLocation(0, 0),
	fVGADevice(-1),
	fListeners(20),
	fRetraceTimeouts(0),
	fRetraceIgnoredUntil(0),
	fFlushLock("vertical blank flush"),
	fFlushTargets(NULL),
	fFlushTargetCount(0),
	fFlushPerOutput(false),
	fFlushGeneration(0),
	fFlushDoneSem(-1),
	fQuitFlush(false),
	fFlushing(false),
	fFlushKnobThread(-1),
	fFlushWanted(false),
	fShadowCurrent(0),
	fShadowReaders(0),
	fDeviceCopyOff(false)
{
	for (int32 i = 0; i < 32; i++)
		fShadowWrites[i].used = false;
}


HWInterface::~HWInterface()
{
	// the derived class, with the buffers, is gone: no draining here
	_AbandonFlushing();
}


status_t
HWInterface::Initialize()
{
	return MultiLocker::InitCheck();
}


DrawingEngine*
HWInterface::CreateDrawingEngine()
{
	return new(std::nothrow) DrawingEngine(this);
}


EventStream*
HWInterface::CreateEventStream()
{
	return NULL;
}


status_t
HWInterface::GetAccelerantPath(BString &path)
{
	return B_ERROR;
}


status_t
HWInterface::GetDriverPath(BString &path)
{
	return B_ERROR;
}


void
HWInterface::OnSeatEnabled()
{
}


void
HWInterface::OnSeatDisabled()
{
}


status_t
HWInterface::GetPreferredMode(display_mode* mode)
{
	return B_NOT_SUPPORTED;
}


status_t
HWInterface::GetMonitorInfo(monitor_info* info)
{
	return B_NOT_SUPPORTED;
}


// #pragma mark -


void
HWInterface::SetCursor(ServerCursor* cursor)
{
	if (!LockParallelAccess())
		return;
	if (!fFloatingOverlaysLock.Lock()) {
		UnlockParallelAccess();
		return;
	}

	if (fSourceCursor.Get() != cursor || fCursor.Get() == NULL
		|| (fCursor->PixelScale() != RenderScaleFactor() && cursor != NULL)) {
		BRect oldFrame = _CursorFrame();

		fSourceCursor.SetTo(cursor);
		if (cursor != NULL && cursor->PixelScale() != RenderScaleFactor())
			fCursor.SetTo(_CursorAtRenderScale(cursor), true);
		else
			fCursor.SetTo(cursor);

		Invalidate(oldFrame);

		_AdoptDragBitmap();
		Invalidate(_CursorFrame());
	}

	fFloatingOverlaysLock.Unlock();
	UnlockParallelAccess();
}


ServerCursorReference
HWInterface::Cursor() const
{
	if (!fFloatingOverlaysLock.Lock())
		return ServerCursorReference(NULL);

	fFloatingOverlaysLock.Unlock();
	return fCursor;
}


ServerCursorReference
HWInterface::CursorAndDragBitmap() const
{
	if (!fFloatingOverlaysLock.Lock())
		return ServerCursorReference(NULL);

	fFloatingOverlaysLock.Unlock();
	return fCursorAndDragBitmap;
}


void
HWInterface::SetCursorVisible(bool visible)
{
	if (!LockParallelAccess())
		return;
	if (!fFloatingOverlaysLock.Lock()) {
		UnlockParallelAccess();
		return;
	}

	if (fCursorVisible != visible) {
		// NOTE: _CursorFrame() will
		// return an invalid rect if
		// fCursorVisible == false!
		if (visible) {
			fCursorVisible = visible;
			fCursorObscured = false;
			IntRect r = _CursorFrame();

			_DrawCursor(r);
			Invalidate(r);
		} else {
			IntRect r = _CursorFrame();
			fCursorVisible = visible;

			_RestoreCursorArea();
			Invalidate(r);
		}
	}

	fFloatingOverlaysLock.Unlock();
	UnlockParallelAccess();
}


bool
HWInterface::IsCursorVisible()
{
	bool visible = true;
	if (fFloatingOverlaysLock.Lock()) {
		visible = fCursorVisible;
		fFloatingOverlaysLock.Unlock();
	}
	return visible;
}


void
HWInterface::ObscureCursor()
{
	if (!LockExclusiveAccess())
		return;
	if (!fFloatingOverlaysLock.Lock()) {
		UnlockExclusiveAccess();
		return;
	}

	if (!fCursorObscured) {
		SetCursorVisible(false);
		fCursorObscured = true;
	}

	fFloatingOverlaysLock.Unlock();
	UnlockExclusiveAccess();
}


void
HWInterface::MoveCursorTo(float x, float y)
{
	if (!LockParallelAccess())
		return;
	if (!fFloatingOverlaysLock.Lock()) {
		UnlockParallelAccess();
		return;
	}

	BPoint p(x, y);
	if (p != fCursorLocation) {
		// unhide cursor if it is obscured only
		if (fCursorObscured) {
			fFloatingOverlaysLock.Unlock();
			UnlockParallelAccess();

			SetCursorVisible(true);

			if (!LockParallelAccess())
				return;
			fFloatingOverlaysLock.Lock();
		}

		IntRect oldFrame = _CursorFrame();
		fCursorLocation = p;
		if (fCursorVisible) {
			// Invalidate and _DrawCursor would not draw
			// anything if the cursor is hidden
			// (invalid cursor frame), but explicitly
			// testing for it here saves us some cycles
			if (fCursorAreaBackup.IsSet()) {
				// means we have a software cursor which we need to draw
				_RestoreCursorArea();
				_DrawCursor(_CursorFrame());
			}
			IntRect newFrame = _CursorFrame();
			if (newFrame.Intersects(oldFrame))
				Invalidate(oldFrame | newFrame);
			else {
				Invalidate(oldFrame);
				Invalidate(newFrame);
			}
		}
	}

	fFloatingOverlaysLock.Unlock();
	UnlockParallelAccess();
}


BPoint
HWInterface::CursorPosition()
{
	BPoint location;
	if (fFloatingOverlaysLock.Lock()) {
		location = fCursorLocation;
		fFloatingOverlaysLock.Unlock();
	}
	return location;
}


void
HWInterface::SetDragBitmap(const ServerBitmap* bitmap,
	const BPoint& offsetFromCursor)
{
	if (fFloatingOverlaysLock.Lock()) {
		fDragBitmap.SetTo((ServerBitmap*)bitmap, false);
		fDragBitmapOffset = offsetFromCursor;
		_AdoptDragBitmap();
		fFloatingOverlaysLock.Unlock();
	}
}


status_t
HWInterface::GetDisplayOutputs(display_output** _outputs, uint32* _count)
{
	return B_UNSUPPORTED;
}


status_t
HWInterface::SetSoftwareScale(uint16 percent)
{
	if (percent < 100 || percent > 400)
		return B_BAD_VALUE;
	fSoftwareScale = percent;
	return B_OK;
}


status_t
HWInterface::SetRenderScale(uint16 percent)
{
	if (percent < 100 || percent > 400)
		return B_BAD_VALUE;
	if (percent == fRenderScale)
		return B_OK;
	fRenderScale = percent;
	gRenderScale = percent / 100.0f;

	// the cursor bitmap is kept at the buffer's density
	if (fFloatingOverlaysLock.Lock()) {
		if (fSourceCursor.Get() != NULL) {
			if (fSourceCursor->PixelScale() != RenderScaleFactor())
				fCursor.SetTo(_CursorAtRenderScale(fSourceCursor.Get()), true);
			else
				fCursor = fSourceCursor;
		}
		_AdoptDragBitmap();
		fFloatingOverlaysLock.Unlock();
	}
	return B_OK;
}


void
HWInterface::SetLogicalSize(int32 width, int32 height)
{
	fLogicalWidth = width;
	fLogicalHeight = height;
}


/*!	The logical size is what the layout says when there is one; otherwise
	the front buffer is the logical size times the software scale or the
	render scale.
*/
int32
HWInterface::LogicalWidth() const
{
	if (fLogicalWidth > 0)
		return fLogicalWidth;
	RenderingBuffer* front = FrontBuffer();
	if (front == NULL)
		return 0;
	uint16 scale = fSoftwareScale != 100 ? fSoftwareScale : fRenderScale;
	return (front->Width() * 100 + scale / 2) / scale;
}


int32
HWInterface::LogicalHeight() const
{
	if (fLogicalHeight > 0)
		return fLogicalHeight;
	RenderingBuffer* front = FrontBuffer();
	if (front == NULL)
		return 0;
	uint16 scale = fSoftwareScale != 100 ? fSoftwareScale : fRenderScale;
	return (front->Height() * 100 + scale / 2) / scale;
}


int32
HWInterface::BackBufferWidth() const
{
	RenderingBuffer* front = FrontBuffer();
	if (front == NULL)
		return 0;
	if (fSoftwareScale == 100)
		return front->Width();
	return (LogicalWidth() * fRenderScale + 50) / 100;
}


int32
HWInterface::BackBufferHeight() const
{
	RenderingBuffer* front = FrontBuffer();
	if (front == NULL)
		return 0;
	if (fSoftwareScale == 100)
		return front->Height();
	return (LogicalHeight() * fRenderScale + 50) / 100;
}


status_t
HWInterface::GetDisplayOutputModes(uint32 id, display_mode** _modes,
	uint32* _count)
{
	return B_UNSUPPORTED;
}


status_t
HWInterface::SetDisplayLayout(const display_output_config* configs,
	uint32 count, bool switchMode)
{
	return B_UNSUPPORTED;
}


// #pragma mark -


RenderingBuffer*
HWInterface::DrawingBuffer() const
{
	if (IsDoubleBuffered())
		return BackBuffer();
	return FrontBuffer();
}


/*!	Waits for the display's next vertical blank, so that drawing that went
	into the back buffer is copied to the screen while the display is not
	showing it (B_WAIT_FOR_RETRACE). The copy then starts ahead of the scan
	and outpaces it, and the picture does not tear.

	The caller holds the interface read locked: other windows draw on, but
	whoever wants it exclusively waits behind this. A display that stops
	reporting blanks (switched off, say) would cost every such drawing the
	whole timeout; after a few timeouts in a row the waits stop for a second.
*/
void
HWInterface::WaitForRetraceBeforeCopy()
{
	if (fFlushing) {
		// every copy waits for the blank
		return;
	}
	if (system_time() < atomic_get64(&fRetraceIgnoredUntil))
		return;

	// a blank missed at 60 Hz still fits, and one at 24 Hz
	status_t status = WaitForRetrace(50000);
	if (status != B_TIMED_OUT) {
		// waited, or there is nothing to wait for
		atomic_set(&fRetraceTimeouts, 0);
		return;
	}

	if (atomic_add(&fRetraceTimeouts, 1) + 1 >= 3) {
		atomic_set(&fRetraceTimeouts, 0);
		atomic_set64(&fRetraceIgnoredUntil, system_time() + 1000000);
	}
}


/*!	The drawing buffer is out of date in \a region (frame buffer pixels):
	a direct window put newer pixels only into the frame buffer there. Only
	where the drawing buffer is a copy of the screen of its own (double
	buffered).
*/
void
HWInterface::MarkStale(const BRegion& region)
{
	if (!IsDoubleBuffered() || region.CountRects() == 0)
		return;
	BAutolock _(fStaleLock);
	fStaleRegion.Include(&region);
	if (BackBuffer() != NULL) {
		BRegion bounds((BRect)BackBuffer()->Bounds());
		fStaleRegion.IntersectWith(&bounds);
	}
	atomic_set(&fHasStale, fStaleRegion.CountRects() > 0 ? 1 : 0);
}


/*!	Before the drawing buffer is read as the screen's contents in \a region
	(moved, read back): its out of date parts there are taken from the frame
	buffer, and are current from then on. Read locked.
*/
void
HWInterface::RefreshStale(const BRegion& region)
{
	if (atomic_get(&fHasStale) == 0)
		return;
	RenderingBuffer* front = FrontBuffer();
	RenderingBuffer* back = BackBuffer();
	if (front == NULL || back == NULL || front == back)
		return;

	BAutolock _(fStaleLock);
	BRegion refresh(fStaleRegion);
	refresh.IntersectWith(&region);
	BRegion bounds((BRect)front->Bounds());
	refresh.IntersectWith(&bounds);
	int32 count = refresh.CountRects();
	if (count == 0)
		return;

	uint32 frontBPR = front->BytesPerRow();
	uint32 backBPR = back->BytesPerRow();
	for (int32 i = 0; i < count; i++) {
		clipping_rect r = refresh.RectAtInt(i);
		size_t bytes = (r.right - r.left + 1) * 4;
		const uint8* src = (const uint8*)front->Bits() + r.top * frontBPR
			+ r.left * 4;
		uint8* dst = (uint8*)back->Bits() + r.top * backBPR + r.left * 4;
		for (int32 y = r.top; y <= r.bottom; y++) {
			memcpy(dst, src, bytes);
			src += frontBPR;
			dst += backBPR;
		}
	}
	fStaleRegion.Exclude(&refresh);
	atomic_set(&fHasStale, fStaleRegion.CountRects() > 0 ? 1 : 0);
}


//!	The buffers change (a mode change): nothing in them is out of date.
void
HWInterface::ForgetStale()
{
	BAutolock _(fStaleLock);
	fStaleRegion.MakeEmpty();
	atomic_set(&fHasStale, 0);
}


/*! The object needs to be already locked!
*/
status_t
HWInterface::InvalidateRegion(const BRegion& region)
{
	int32 count = region.CountRects();
	for (int32 i = 0; i < count; i++) {
		status_t result = Invalidate(region.RectAt(i));
		if (result != B_OK)
			return result;
	}

	return B_OK;
}


/*! The object needs to be already locked!
*/
status_t
HWInterface::Invalidate(const BRect& frame)
{
	_NotifyFrameCommitted(frame);

	if (atomic_get(&fHasStale) != 0) {
		// drawn into: the drawing buffer is the current one there
		BAutolock _(fStaleLock);
		fStaleRegion.Exclude(frame);
		atomic_set(&fHasStale, fStaleRegion.CountRects() > 0 ? 1 : 0);
	}
	if (IsDoubleBuffered())
		return CopyBackToFront(frame);

	return B_OK;
}


/*! The object must already be locked!
*/
status_t
HWInterface::CopyBackToFront(const BRect& frame)
{
	RenderingBuffer* frontBuffer = FrontBuffer();
	RenderingBuffer* backBuffer = BackBuffer();

	if (!backBuffer || !frontBuffer)
		return B_NO_INIT;

	// we need to mess with the area, but it is const
	IntRect area(frame);
	IntRect bufferClip(backBuffer->Bounds());

	if (!area.IsValid() || !area.Intersects(bufferClip))
		return B_BAD_VALUE;

	// make sure we don't copy out of bounds
	area = bufferClip & area;

	if (fFlushing) {
		// what is drawn goes into the current shadow now, from which the
		// flush copies at a blank: a drawing op under way by then does not
		// get into the frame
		int32 slot = -1;
		int32 which = 0;
		RenderingBuffer* shadow = NULL;
		if (fFlushLock.Lock()) {
			which = fShadowCurrent;
			shadow = fFlushShadow[which].Get();
			if (shadow != NULL && (shadow->Width() != backBuffer->Width()
					|| shadow->Height() != backBuffer->Height()))
				shadow = NULL;
			if (shadow != NULL) {
				for (int32 i = 0; i < 32; i++) {
					if (!fShadowWrites[i].used) {
						fShadowWrites[i].rect = area;
						fShadowWrites[i].shadow = which;
						fShadowWrites[i].used = true;
						slot = i;
						break;
					}
				}
			}
			fFlushLock.Unlock();
		}
		if (shadow != NULL && slot >= 0 && shadow->Bits() == NULL) {
			// a shadow in the device's memory: the device copies, the CPU
			// is free (or, when it cannot, the flush copies from the back
			// buffer from now on)
			_CopyBackToShadowByDevice(shadow, area);
		} else if (shadow != NULL && slot >= 0) {
			uint32 srcBPR = backBuffer->BytesPerRow();
			uint32 dstBPR = shadow->BytesPerRow();
			uint8* src = (uint8*)backBuffer->Bits() + area.top * srcBPR
				+ area.left * 4;
			uint8* dst = (uint8*)shadow->Bits() + area.top * dstBPR
				+ area.left * 4;
			size_t bytes = (area.right - area.left + 1) * 4;
			for (int32 y = area.top; y <= area.bottom; y++) {
				memcpy(dst, src, bytes);
				src += srcBPR;
				dst += dstBPR;
			}
		}
		if (_QueueForFlush(area, slot, which))
			return B_OK;
	}

	return _CopyAreaToFront(area);
}


/*!	Copies the area (within the back buffer) to the screen now, leaving out
	the cursor's place and drawing the cursor over it. Read locked.
*/
status_t
HWInterface::_CopyAreaToFront(const IntRect& area)
{
	bool cursorLocked = fFloatingOverlaysLock.Lock();

	BRegion region((BRect)area);
	if (IsDoubleBuffered())
		region.Exclude((clipping_rect)_CursorFrame());

	_CopyBackToFront(region);

	_DrawCursor(area);

	if (cursorLocked)
		fFloatingOverlaysLock.Unlock();

	return B_OK;
}


void
HWInterface::_CopyBackToFront(/*const*/ BRegion& region)
{
	RenderingBuffer* backBuffer = BackBuffer();

	uint32 srcBPR = backBuffer->BytesPerRow();
	uint8* src = (uint8*)backBuffer->Bits();

	int32 count = region.CountRects();
	for (int32 i = 0; i < count; i++) {
		clipping_rect r = region.RectAtInt(i);
		// offset to left top pixel in source buffer (always B_RGBA32)
		uint8* srcOffset = src + r.top * srcBPR + r.left * 4;
		_CopyToFront(srcOffset, srcBPR, r.left, r.top, r.right, r.bottom);
	}
}


/*!	The flush's copy: from the shadow of the back buffer (what was drawn
	when it was queued), or from the back buffer itself without one. */
void
HWInterface::_CopyShadowToFront(BRegion& region, int32 which)
{
	RenderingBuffer* shadow = fFlushShadow[which].Get();
	RenderingBuffer* backBuffer = BackBuffer();
	if (shadow == NULL || backBuffer == NULL
		|| shadow->Width() != backBuffer->Width()
		|| shadow->Height() != backBuffer->Height()) {
		_CopyBackToFront(region);
		return;
	}

	// the device copies faster than the scan goes down the screen, and
	// without the CPU; done when it returns
	RenderingBuffer* frontBuffer = FrontBuffer();
	if (!fDeviceCopyOff && frontBuffer != NULL
		&& frontBuffer->Width() == shadow->Width()
		&& frontBuffer->Height() == shadow->Height()
		&& _CopyShadowByDevice(shadow, region)) {
		return;
	}

	if (shadow->Bits() == NULL) {
		// the CPU cannot read a shadow in the device's memory (device
		// copies switched off, or failed): the back buffer as it is now
		_CopyBackToFront(region);
		return;
	}

	// in bands of rows across all the rects, so that the copy goes down
	// the screen as the scan does: rects side by side would otherwise be
	// copied one after the other, the later ones behind the scan
	const int32 kBandRows = 64;
	uint32 srcBPR = shadow->BytesPerRow();
	uint8* src = (uint8*)shadow->Bits();
	int32 count = region.CountRects();
	clipping_rect frame = region.FrameInt();
	for (int32 top = frame.top; top <= frame.bottom; top += kBandRows) {
		int32 bottom = min_c(top + kBandRows - 1, frame.bottom);
		for (int32 i = 0; i < count; i++) {
			clipping_rect r = region.RectAtInt(i);
			if (r.top > bottom || r.bottom < top)
				continue;
			int32 from = max_c(r.top, top);
			int32 to = min_c(r.bottom, bottom);
			uint8* srcOffset = src + from * srcBPR + r.left * 4;
			_CopyToFront(srcOffset, srcBPR, r.left, from, r.right, to);
		}
	}
}


// #pragma mark - vertical blank flush


/*!	Copies from the back buffer to the front one are not made as the windows
	draw: they are gathered and made by a thread once a frame, right after
	the display's vertical blank. A copy that starts at the blank runs ahead
	of the scan (it is faster), so what the display shows is never half of
	one picture and half of the next: windows move and scroll without
	tearing. Each display output has its own target, with its blanks and the
	part of the frame buffer it shows, when the accelerant tells them apart;
	else one target, the whole frame buffer, with the accelerant's one
	retrace semaphore. The threads keep the interface read locked while they
	copy, as a drawing window would.

	Without any retrace semaphore the copies are made at once, as before.
*/
struct HWInterface::FlushTarget {
	HWInterface*	interface;
	uint32			outputID;		// 0: the whole frame buffer
	char			name[B_DISPLAY_OUTPUT_NAME_LENGTH];
	IntRect			frame;			// what it shows, in buffer pixels
									// (invalid: not driven)
	sem_id			retraceSemaphore;	// -1: not asked for yet
	int32			scanLineKnown;	// -1: not asked yet; the hardware
									// tells where its scan is
	bigtime_t		scanStart;		// when its frame's scan began, and
	bigtime_t		scanPeriod;		// how long a frame takes (from it)
	bigtime_t		period;			// between blanks
	bigtime_t		modePeriod;		// a frame by the timing it is driven
									// with (0: not known); written under
									// fFlushLock
	IntRect			scanFrame;		// what the scan runs over (the
									// target's frame, or the buffer)
	thread_id		thread;
	sem_id			wake;			// something is pending
	BRegion			pending[2];		// per shadow, fFlushLock
	uint32			queued;			// counts, fFlushLock: each queueing,
	uint32			flushed;		// and the queueing a flush got to
	FlushWorker*	workers;
	int32			workerCount;

	int32			timeouts;
	bigtime_t		ignoredUntil;
	bigtime_t		lastBlank;
	int64			wakes;			// the thread's rounds (for the dump)
	bigtime_t		lastWake;

	int64			flushes;
	int64			rects;
	int64			late;
	int32			lateReports;	// per minute
	bigtime_t		wokeLate;		// this flush: the thread's wake after
									// the blank it aimed at, and the wait
	bigtime_t		waitedWrites;	// for shadow writes under way
	bigtime_t		waitedLocks;	// and for the interface and cursor
									// locks
	float			worstLate;
	bigtime_t		longest;
	bigtime_t		reported;
};


/*!	A thread that copies one band of a flush, so that a large one is done
	by several at once and is over sooner: with N bands each has N times
	the scan's time per row, and all of them stay ahead of it when the whole
	copy takes less than a frame.
*/
struct HWInterface::FlushWorker {
	HWInterface*	interface;
	thread_id		thread;
	sem_id			start;
	BRegion			region;
	bigtime_t		began;
	bigtime_t		ended;
	float			beganRow;		// where the scan was then
	float			endedRow;
	FlushTarget*	target;
	bigtime_t		blank;
	int32			shadow;
};


sem_id
HWInterface::OutputRetraceSemaphore(uint32 id)
{
	return B_UNSUPPORTED;
}


status_t
HWInterface::OutputScanLine(uint32 id, int32* _line, int32* _lines,
	bigtime_t* _scanStart, bigtime_t* _period)
{
	return B_UNSUPPORTED;
}


/*!	The buffer row the target's scan is at right now: from the hardware when
	it tells (a row above the frame while it is in the blank), else
	estimated from the time since the blank over the frame's rows (which
	leaves the blank's own lines as a margin). Without a blank, unknown. */
float
HWInterface::_FlushScanRow(FlushTarget& target, bigtime_t blank)
{
	if (blank != 0 && system_time() < blank) {
		// before the scan starts (the flush begins a little early): the
		// previous frame's scan, in its last rows, is above everything
		return target.scanFrame.top - 1e6;
	}
	if (target.scanLineKnown != 0 && target.outputID != 0) {
		int32 line, lines;
		bigtime_t scanStart, period;
		status_t status = OutputScanLine(target.outputID, &line, &lines,
			&scanStart, &period);
		if (status == B_OK && lines > 0) {
			target.scanLineKnown = 1;
			target.scanStart = scanStart;
			target.scanPeriod = period;
			return target.scanFrame.top
				+ float(line) * (target.scanFrame.Height() + 1) / lines;
		}
		if (status == B_UNSUPPORTED)
			target.scanLineKnown = 0;
	}
	if (blank == 0)
		return -1e9;
	return target.scanFrame.top + float(system_time() - blank) / target.period
		* (target.scanFrame.Height() + 1);
}


/*!	Stops the threads without a last copy: for the destructor, when the
	buffers are gone already. */
void
HWInterface::_AbandonFlushing()
{
	if (fFlushTargets == NULL)
		return;
	debug_printf("app_server: vertical blank flush: abandoned\n");
	fFlushing = false;
	fQuitFlush = true;
	if (fFlushKnobThread >= 0) {
		status_t result;
		wait_for_thread(fFlushKnobThread, &result);
		fFlushKnobThread = -1;
	}
	for (int32 i = 0; i < fFlushTargetCount; i++) {
		FlushTarget& target = fFlushTargets[i];
		delete_sem(target.wake);
		status_t result;
		wait_for_thread(target.thread, &result);
		_StopFlushWorkers(target);
	}
	delete[] fFlushTargets;
	fFlushTargets = NULL;
	fFlushTargetCount = 0;
	if (fFlushDoneSem >= 0) {
		delete_sem(fFlushDoneSem);
		fFlushDoneSem = -1;
	}
}


static const int32 kMaxFlushTargets = 16;
static const bigtime_t kDefaultFramePeriod = 16667;


/*!	The scan timing of the output that shows the most of \a screenFrame
	(of the whole screen when the outputs share one blank), for programs that
	write into the frame buffer themselves. From the hardware when it tells;
	else from the last blank the flush saw, and when that is old, from one
	waited for here (up to two frames).
*/
status_t
HWInterface::GetScanTiming(BRect screenFrame, display_scan_timing& timing)
{
	timing = display_scan_timing();

	float factor = RenderScaleFactor();
	IntRect area((int32)floorf(screenFrame.left * factor),
		(int32)floorf(screenFrame.top * factor),
		(int32)ceilf((screenFrame.right + 1) * factor) - 1,
		(int32)ceilf((screenFrame.bottom + 1) * factor) - 1);

	if (!fFlushLock.Lock())
		return B_ERROR;
	if (fFlushTargets == NULL || fFlushTargetCount == 0) {
		fFlushLock.Unlock();
		return B_UNSUPPORTED;
	}
	FlushTarget* best = &fFlushTargets[0];
	if (fFlushPerOutput) {
		int64 bestOverlap = -1;
		for (int32 i = 1; i < fFlushTargetCount; i++) {
			FlushTarget& target = fFlushTargets[i];
			if (!target.frame.IsValid())
				continue;
			IntRect overlap = target.frame & area;
			int64 size = overlap.IsValid()
				? int64(overlap.Width() + 1) * (overlap.Height() + 1) : 0;
			if (size > bestOverlap) {
				bestOverlap = size;
				best = &target;
			}
		}
	}
	uint32 outputID = best->outputID;
	IntRect frame = best->frame;
	int32 scanLineKnown = best->scanLineKnown;
	sem_id semaphore = best->retraceSemaphore;
	bigtime_t lastBlank = best->lastBlank;
	bigtime_t period = best->modePeriod > 0 ? best->modePeriod
		: kDefaultFramePeriod;
	timing.generation = fFlushGeneration;
	fFlushLock.Unlock();

	timing.output_id = outputID;
	timing.device_frame = (clipping_rect)frame;
	timing.frame = BRect(frame.left / factor, frame.top / factor,
		(frame.right + 1) / factor - 1, (frame.bottom + 1) / factor - 1);
	timing.period = period;

	if (outputID != 0 && scanLineKnown != 0) {
		int32 line, lines;
		bigtime_t scanStart, scanPeriod;
		if (OutputScanLine(outputID, &line, &lines, &scanStart, &scanPeriod)
				== B_OK && scanStart != 0 && scanPeriod > 0) {
			timing.scan_start = scanStart;
			timing.period = scanPeriod;
			timing.flags = B_SCAN_TIMING_FROM_HARDWARE;
			return B_OK;
		}
	}

	bigtime_t now = system_time();
	if (lastBlank != 0 && now - lastBlank < 1000000) {
		timing.scan_start = lastBlank;
		timing.flags = B_SCAN_TIMING_FROM_BLANK;
		return B_OK;
	}

	// nothing was flushed lately: see a blank (the semaphore lets every
	// waiter go, the flush thread's wait is not disturbed)
	if (semaphore == -1) {
		semaphore = outputID == 0
			? RetraceSemaphore() : OutputRetraceSemaphore(outputID);
	}
	if (semaphore >= 0) {
		status_t status;
		do {
			status = acquire_sem_etc(semaphore, 1, B_RELATIVE_TIMEOUT,
				2 * period + 2000);
		} while (status == B_INTERRUPTED);
		if (status == B_OK) {
			timing.scan_start = system_time();
			timing.flags = B_SCAN_TIMING_FROM_BLANK;
		}
	}
	return B_OK;
}


/*!	How long a frame takes with this timing, in microseconds; 0 when the
	timing does not say (or says something no display does). */
static bigtime_t
frame_period(const display_timing& timing)
{
	if (timing.pixel_clock == 0 || timing.h_total == 0 || timing.v_total == 0)
		return 0;
	bigtime_t period = (bigtime_t)timing.h_total * timing.v_total * 1000
		/ timing.pixel_clock;
	if ((timing.flags & B_TIMING_INTERLACED) != 0)
		period /= 2;
	return period >= 2000 && period <= 60000 ? period : 0;
}


/*!	Starts the target for the whole screen; the outputs' targets are added
	as the mode sets tell of them (_UpdateFlushTargets()). */
status_t
HWInterface::_StartFlushing()
{
	if (fFlushTargets != NULL)
		return B_OK;


	fFlushDoneSem = create_sem(0, "vertical blank flush done");
	if (fFlushDoneSem < 0)
		return fFlushDoneSem;
	fFlushTargets = new(std::nothrow) FlushTarget[kMaxFlushTargets];
	if (fFlushTargets == NULL) {
		delete_sem(fFlushDoneSem);
		fFlushDoneSem = -1;
		return B_NO_MEMORY;
	}
	fQuitFlush = false;
	status_t status = _AddFlushTarget(0, "screen");
	if (status != B_OK) {
		debug_printf("app_server: vertical blank flush: no thread: %s\n",
			strerror(status));
		delete[] fFlushTargets;
		fFlushTargets = NULL;
		delete_sem(fFlushDoneSem);
		fFlushDoneSem = -1;
		return status;
	}

	fFlushWanted = true;
	fFlushing = true;
	_UpdateFlushTargets();

	// for comparisons: a file named no_vblank_flush in the user's
	// app_server settings switches the flush off while it exists
	fFlushKnobThread = spawn_thread(_FlushKnobEntry,
		"vertical blank flush switch", B_LOW_PRIORITY, this);
	if (fFlushKnobThread >= 0)
		resume_thread(fFlushKnobThread);
	return B_OK;
}


/*static*/ status_t
HWInterface::_FlushKnobEntry(void* data)
{
	((HWInterface*)data)->_FlushKnobThread();
	return B_OK;
}


void
HWInterface::_FlushKnobThread()
{
	BPath settings;
	if (find_directory(B_USER_SETTINGS_DIRECTORY, &settings) != B_OK)
		return;
	BPath deviceCopy(settings);
	BPath dump(settings);
	settings.Append("app_server/no_vblank_flush");
	deviceCopy.Append("app_server/no_device_copy");
	dump.Append("app_server/vblank_flush_dump");
	while (!fQuitFlush) {
		// for diagnosis: the flush's state goes to the syslog when the file
		// is created (it is removed again)
		BEntry dumpEntry(dump.Path());
		if (dumpEntry.Exists()) {
			dumpEntry.Remove();
			_DumpFlushState();
		}

		bool off = BEntry(settings.Path()).Exists();
		if (off == fFlushWanted) {
			debug_printf("app_server: vertical blank flush: %s (%s)\n",
				off ? "off" : "on", settings.Path());
			_SetFlushing(!off);
		}
		// the CPU copies again from the device's shadows while it exists
		bool cpu = BEntry(deviceCopy.Path()).Exists();
		if (cpu != fDeviceCopyOff) {
			if (_DeviceCopiesShadows() || cpu) {
				debug_printf("app_server: vertical blank flush: copies by "
					"the %s (%s)\n", cpu ? "CPU" : "device", deviceCopy.Path());
			}
			fDeviceCopyOff = cpu;
		}
		snooze(500000);
	}
}


/*!	The flush's state in the syslog, for diagnosis (see _FlushKnobThread()).
*/
void
HWInterface::_DumpFlushState()
{
	if (!fFlushLock.Lock())
		return;
	bigtime_t now = system_time();
	RenderingBuffer* backBuffer = BackBuffer();
	debug_printf("app_server: flush dump: flushing %d wanted %d per output %d"
		" shadow current %" B_PRId32 " readers %" B_PRId32 " back %" B_PRIu32
		"x%" B_PRIu32 "\n", fFlushing, fFlushWanted, fFlushPerOutput,
		fShadowCurrent, fShadowReaders,
		backBuffer != NULL ? backBuffer->Width() : 0,
		backBuffer != NULL ? backBuffer->Height() : 0);
	for (int32 i = 0; i < 2; i++) {
		RenderingBuffer* shadow = fFlushShadow[i].Get();
		debug_printf("app_server: flush dump: shadow %" B_PRId32 ": %" B_PRIu32
			"x%" B_PRIu32 "%s\n", i, shadow != NULL ? shadow->Width() : 0,
			shadow != NULL ? shadow->Height() : 0,
			shadow == NULL ? " (none)"
				: shadow->Bits() == NULL ? " (device)" : " (memory)");
	}
	int32 writes = 0;
	for (int32 i = 0; i < 32; i++) {
		if (fShadowWrites[i].used)
			writes++;
	}
	debug_printf("app_server: flush dump: %" B_PRId32 " shadow writes under "
		"way\n", writes);
	for (int32 i = 0; i < fFlushTargetCount; i++) {
		FlushTarget& target = fFlushTargets[i];
		clipping_rect p0 = target.pending[0].FrameInt();
		clipping_rect p1 = target.pending[1].FrameInt();
		debug_printf("app_server: flush dump: %" B_PRId32 " %s (id %" B_PRIu32
			"): frame %" B_PRId32 ",%" B_PRId32 "-%" B_PRId32 ",%" B_PRId32
			" sem %" B_PRId32 " scan %" B_PRId32 " pending %" B_PRId32
			" (%" B_PRId32 ",%" B_PRId32 "-%" B_PRId32 ",%" B_PRId32 ") / %"
			B_PRId32 " (%" B_PRId32 ",%" B_PRId32 "-%" B_PRId32 ",%" B_PRId32
			") queued %" B_PRIu32 " flushed %" B_PRIu32 " wakes %" B_PRId64
			" last %.1f s ago, flushes %" B_PRId64 ", ignored for %.1f s\n",
			i, target.name, target.outputID, target.frame.left,
			target.frame.top, target.frame.right, target.frame.bottom,
			target.retraceSemaphore, target.scanLineKnown,
			target.pending[0].CountRects(), p0.left, p0.top, p0.right,
			p0.bottom, target.pending[1].CountRects(), p1.left, p1.top,
			p1.right, p1.bottom, target.queued, target.flushed, target.wakes,
			target.lastWake != 0 ? (now - target.lastWake) / 1000000.0 : -1.0,
			target.flushes,
			target.ignoredUntil > now
				? (target.ignoredUntil - now) / 1000000.0 : 0.0);
	}
	fFlushLock.Unlock();
}


/*!	Switches the flush off (copies as drawn from now on, whatever is
	pending copied now) or on again. */
void
HWInterface::_SetFlushing(bool flushing)
{
	if (fFlushLock.Lock()) {
		fFlushWanted = flushing;
		fFlushing = flushing && fFlushTargets != NULL;
		fFlushLock.Unlock();
	}
	if (!flushing && fFlushTargets != NULL) {
		for (int32 i = 0; i < fFlushTargetCount; i++)
			release_sem_etc(fFlushTargets[i].wake, 1, 0);
	}
}


status_t
HWInterface::_AddFlushTarget(uint32 outputID, const char* name)
{
	if (fFlushTargetCount >= kMaxFlushTargets)
		return B_NO_MEMORY;

	FlushTarget& target = fFlushTargets[fFlushTargetCount];
	target.interface = this;
	target.outputID = outputID;
	strlcpy(target.name, name, sizeof(target.name));
	target.frame = IntRect(0, 0, -1, -1);
	target.retraceSemaphore = -1;
	target.scanLineKnown = -1;
	target.scanStart = 0;
	target.scanPeriod = 0;
	target.scanFrame = IntRect(0, 0, -1, -1);
	target.workers = NULL;
	target.workerCount = 0;
	target.timeouts = 0;
	target.ignoredUntil = 0;
	target.lastBlank = 0;
	target.wakes = 0;
	target.lastWake = 0;
	target.period = kDefaultFramePeriod;
	target.modePeriod = 0;
	target.flushes = target.rects = target.late = 0;
	target.lateReports = 0;
	target.wokeLate = 0;
	target.waitedWrites = 0;
	target.waitedLocks = 0;
	target.worstLate = 0;
	target.longest = target.reported = 0;
	target.queued = target.flushed = 0;
	target.wake = create_sem(0, "vertical blank flush");
	if (target.wake < 0)
		return target.wake;
	target.thread = spawn_thread(_FlushThreadEntry, "vertical blank flush",
		B_REAL_TIME_DISPLAY_PRIORITY, &target);
	if (target.thread < 0) {
		delete_sem(target.wake);
		target.wake = -1;
		return target.thread;
	}
	fFlushTargetCount++;
	resume_thread(target.thread);
	return B_OK;
}


void
HWInterface::_StopFlushing()
{
	if (fFlushTargets == NULL)
		return;
	debug_printf("app_server: vertical blank flush: stopping\n");

	// from now on the copies are made as drawn (the switch is made under
	// the lock, so that nothing is queued after the last drain)
	if (fFlushLock.Lock()) {
		fFlushing = false;
		fFlushLock.Unlock();
	}
	fQuitFlush = true;
	if (fFlushKnobThread >= 0) {
		status_t result;
		wait_for_thread(fFlushKnobThread, &result);
		fFlushKnobThread = -1;
	}
	for (int32 i = 0; i < fFlushTargetCount; i++) {
		FlushTarget& target = fFlushTargets[i];
		delete_sem(target.wake);
		target.wake = -1;
		status_t result;
		wait_for_thread(target.thread, &result);
		target.thread = -1;
		// whatever was still waiting goes to the screen now (both shadows)
		_FlushPending(target, 0, _FlipShadow());
		_FlushPending(target, 0, _FlipShadow());
		_StopFlushWorkers(target);
	}
	delete[] fFlushTargets;
	fFlushTargets = NULL;
	fFlushTargetCount = 0;
	delete_sem(fFlushDoneSem);
	fFlushDoneSem = -1;
}


/*!	The flush's shadows go, before the device that may own their memory
	does. After _StopFlushing().
*/
void
HWInterface::_ReleaseFlushShadows()
{
	for (int32 i = 0; i < 2; i++)
		fFlushShadow[i].Unset();
}


RenderingBuffer*
HWInterface::_CreateFlushShadow(uint32 width, uint32 height)
{
	return new(std::nothrow) MallocBuffer(width, height);
}


/*!	Tells the targets what their outputs show now: after a mode or layout
	change. Write locked (or not locked yet, when starting).
*/
void
HWInterface::_UpdateFlushTargets()
{
	if (fFlushTargets == NULL)
		return;

	display_output* outputs = NULL;
	uint32 outputCount = 0;
	if (GetDisplayOutputs(&outputs, &outputCount) != B_OK)
		outputCount = 0;

	// a target for every output not seen before
	for (uint32 j = 0; j < outputCount; j++) {
		bool known = false;
		for (int32 i = 1; i < fFlushTargetCount; i++) {
			if (fFlushTargets[i].outputID == outputs[j].id)
				known = true;
		}
		if (!known && outputs[j].id != 0)
			_AddFlushTarget(outputs[j].id, outputs[j].name);
	}

	RenderingBuffer* backBuffer = BackBuffer();
	IntRect bufferFrame = backBuffer != NULL
		? IntRect(backBuffer->Bounds()) : IntRect(0, 0, -1, -1);

	// the shadows, the back buffer's size (the flush copies from them)
	for (int32 i = 0; backBuffer != NULL && i < 2; i++) {
		if (fFlushShadow[i].IsSet()
			&& fFlushShadow[i]->Width() == backBuffer->Width()
			&& fFlushShadow[i]->Height() == backBuffer->Height())
			continue;
		fFlushShadow[i].Unset();
		RenderingBuffer* shadow = _CreateFlushShadow(backBuffer->Width(),
			backBuffer->Height());
		if (shadow != NULL && shadow->InitCheck() == B_OK) {
			// the back buffer as it is, so that nothing stale is copied
			bool filled = true;
			if (shadow->Bits() != NULL) {
				memcpy(shadow->Bits(), backBuffer->Bits(),
					min_c(shadow->BitsLength(), backBuffer->BitsLength()));
			} else {
				filled = _CopyBackToShadowByDevice(shadow,
					IntRect(backBuffer->Bounds()));
			}
			if (filled)
				fFlushShadow[i].SetTo(shadow);
			else
				delete shadow;
		} else
			delete shadow;
	}

	// The screen's one semaphore follows the main display, the one the mode
	// describes.
	display_mode mode = {};
	GetMode(&mode);

	if (fFlushLock.Lock()) {
		fFlushTargets[0].frame = bufferFrame;
		fFlushTargets[0].modePeriod = frame_period(mode.timing);
		bool perOutput = false;
		for (int32 i = 1; i < fFlushTargetCount; i++) {
			FlushTarget& target = fFlushTargets[i];
			target.frame = IntRect(0, 0, -1, -1);
			for (uint32 j = 0; j < outputCount; j++) {
				const display_output& output = outputs[j];
				if (output.id != target.outputID)
					continue;
				target.modePeriod = frame_period(output.timing);
				if ((output.flags & B_DISPLAY_OUTPUT_ENABLED) == 0
					|| (output.flags & B_DISPLAY_OUTPUT_MIRROR) != 0)
					break;
				if (target.retraceSemaphore == -1) {
					target.retraceSemaphore
						= OutputRetraceSemaphore(output.id);
					if (target.retraceSemaphore < 0)
						target.retraceSemaphore = B_UNSUPPORTED;
				}
				if (target.retraceSemaphore < 0)
					break;
				target.frame = bufferFrame & IntRect(output.x, output.y,
					output.x + output.width - 1, output.y + output.height - 1);
				if (target.frame.IsValid())
					perOutput = true;
				break;
			}
		}
		if (perOutput != fFlushPerOutput) {
			debug_printf("app_server: vertical blank flush: %s\n", perOutput
				? "each output at its own blanks" : "the screen at one's");
		}
		fFlushPerOutput = perOutput;
		fFlushGeneration++;
		fFlushLock.Unlock();
	}
	free(outputs);
}


bool
HWInterface::_QueueForFlush(const IntRect& area, int32 shadowSlot,
	int32 which)
{
	if (!fFlushLock.Lock())
		return false;
	if (shadowSlot >= 0)
		fShadowWrites[shadowSlot].used = false;
	if (!fFlushing || shadowSlot < 0) {
		// not in a shadow: copied as drawn
		fFlushLock.Unlock();
		return false;
	}

	// to every output that shows some of it, or else to the whole screen
	bool routed = false;
	if (fFlushPerOutput) {
		for (int32 i = 1; i < fFlushTargetCount; i++) {
			FlushTarget& target = fFlushTargets[i];
			if (!target.frame.IsValid() || !target.frame.Intersects(area))
				continue;
			bool wasEmpty = target.pending[which].CountRects() == 0;
			target.pending[which].Include((clipping_rect)(target.frame & area));
			target.queued++;
			if (wasEmpty)
				release_sem_etc(target.wake, 1, B_DO_NOT_RESCHEDULE);
			routed = true;
		}
	}
	if (!routed) {
		FlushTarget& target = fFlushTargets[0];
		bool wasEmpty = target.pending[which].CountRects() == 0;
		target.pending[which].Include((clipping_rect)area);
		target.queued++;
		if (wasEmpty)
			release_sem_etc(target.wake, 1, B_DO_NOT_RESCHEDULE);
	}
	fFlushLock.Unlock();
	return true;
}


/*static*/ status_t
HWInterface::_FlushThreadEntry(void* data)
{
	FlushTarget* target = (FlushTarget*)data;
	target->interface->_FlushThread(*target);
	return B_OK;
}


void
HWInterface::_FlushThread(FlushTarget& target)
{
	while (!fQuitFlush) {
		// something to copy?
		status_t status;
		do {
			status = acquire_sem(target.wake);
		} while (status == B_INTERRUPTED);
		if (status != B_OK || fQuitFlush)
			break;
		target.wakes++;
		target.lastWake = system_time();

		// the blank
		bigtime_t blank = 0;
		if (!fFlushWanted) {
			// switched off: whatever is pending goes now, from both shadows
			_FlushPending(target, 0, _FlipShadow());
			_FlushPending(target, 0, _FlipShadow());
			continue;
		}
		// The shadows are flipped a few ms before the blank is due, so
		// that a drawing op's copy into the one left behind (a memcpy of
		// a few ms at most) is over by the time the flush copies from it:
		// the copy then starts at the blank, not after a wait.
		bigtime_t now = system_time();
		bigtime_t next = 0;
		bool fromScan = false;
		if (target.scanLineKnown == 1 && target.scanStart != 0
			&& target.scanPeriod > 0 && now - target.scanStart < 1000000) {
			// The hardware told when its scans begin: the copy starts
			// just before the next one, whatever the blank interrupt's
			// own timing (on some GPUs it comes after the scan began).
			next = target.scanStart + ((now - target.scanStart)
				/ target.scanPeriod + 1) * target.scanPeriod;
			if (next - now < 500)
				next += target.scanPeriod;
			fromScan = true;
		} else {
			// A frame takes what the timing the display is driven with says.
			// The time between two wakes on the semaphore is a frame only
			// when no blank went by unseen, which the waiter cannot tell (a
			// period measured that way once stuck at four frames, and the
			// flush, sleeping until the blank it expected, then copied at
			// every fourth). The last blank tells where the frames begin,
			// for a while: the wakes are a little late, the clocks drift.
			target.period = target.modePeriod > 0
				? target.modePeriod : kDefaultFramePeriod;
			if (target.lastBlank != 0 && now - target.lastBlank < 1000000) {
				next = target.lastBlank + ((now - target.lastBlank)
					/ target.period + 1) * target.period;
			}
		}
		if (next != 0) {
			bigtime_t flipAt = next - 4000;
			if (flipAt > now + 200)
				snooze_until(flipAt, B_SYSTEM_TIMEBASE);
		}
		int32 which = _FlipShadow();
		if (fromScan) {
			// A sleeping core can take a millisecond to wake from a deep
			// idle state (the Framework 13 did): sleep until shortly
			// before, then spin for the last stretch.
			bigtime_t wakeAt = next - 1500;
			if (wakeAt > system_time())
				snooze_until(wakeAt, B_SYSTEM_TIMEBASE);
			while (system_time() < next - 500)
				;
			target.wokeLate = system_time() - next;
			blank = next;
			target.lastBlank = next;
			target.period = target.scanPeriod;
			// the semaphore's release for this blank, if any, is not
			// waited for; a stale count is harmless (consumed below)
			if (target.retraceSemaphore >= 0)
				acquire_sem_etc(target.retraceSemaphore, 1, B_RELATIVE_TIMEOUT, 0);
		} else if (system_time() >= target.ignoredUntil) {
			if (target.retraceSemaphore == -1) {
				target.retraceSemaphore = target.outputID == 0
					? RetraceSemaphore()
					: OutputRetraceSemaphore(target.outputID);
				if (target.retraceSemaphore < 0)
					target.retraceSemaphore = B_UNSUPPORTED;
			}
			// a blank missed at 60 Hz still fits, and one at 24 Hz
			status = target.retraceSemaphore;
			while (status >= 0) {
				status = acquire_sem_etc(target.retraceSemaphore, 1,
					B_RELATIVE_TIMEOUT, 50000);
				if (status != B_INTERRUPTED)
					break;
			}
			if (status == B_OK) {
				blank = system_time();
				target.timeouts = 0;
				target.lastBlank = blank;
			} else if (status == B_TIMED_OUT) {
				// the display does not report blanks right now
				if (++target.timeouts >= 3) {
					target.timeouts = 0;
					target.ignoredUntil = system_time() + 1000000;
				}
			} else if (target.outputID == 0) {
				// no retrace semaphore at all: copy as drawn, from now on
				debug_printf("app_server: vertical blank flush: no retrace "
					"semaphore (%s), copies as drawn\n", strerror(status));
				if (fFlushLock.Lock()) {
					fFlushing = false;
					fFlushLock.Unlock();
				}
				_FlushPending(target, 0, which);
				return;
			} else {
				// the output's semaphore went away: copy as drawn, and
				// ask again later
				target.retraceSemaphore = -1;
				target.ignoredUntil = system_time() + 1000000;
			}
		}

		_FlushPending(target, blank, which);
	}
	debug_printf("app_server: vertical blank flush %s: thread ends (%s)\n",
		target.name, fQuitFlush ? "quit" : "wake semaphore gone");
}


/*static*/ status_t
HWInterface::_FlushWorkerEntry(void* data)
{
	FlushWorker* worker = (FlushWorker*)data;
	HWInterface* interface = worker->interface;
	while (true) {
		status_t status;
		do {
			status = acquire_sem(worker->start);
		} while (status == B_INTERRUPTED);
		if (status != B_OK || interface->fQuitFlush)
			break;
		worker->began = system_time();
		worker->beganRow = interface->_FlushScanRow(*worker->target,
			worker->blank);
		interface->_CopyShadowToFront(worker->region, worker->shadow);
		worker->ended = system_time();
		worker->endedRow = interface->_FlushScanRow(*worker->target,
			worker->blank);
		release_sem_etc(interface->fFlushDoneSem, 1, B_DO_NOT_RESCHEDULE);
	}
	return B_OK;
}


/*!	Up to three workers per target, and fewer with few processors, so that
	the targets together do not ask for more than there are.
*/
void
HWInterface::_StartFlushWorkers(FlushTarget& target)
{
	system_info info;
	if (get_system_info(&info) != B_OK || info.cpu_count < 2)
		return;
	int32 outputs = 0;
	for (int32 i = 1; i < fFlushTargetCount; i++) {
		if (fFlushTargets[i].frame.IsValid())
			outputs++;
	}
	int32 count = ((int32)info.cpu_count - 1) / max_c(outputs, 1);
	count = min_c(count, 3);
	if (count < 1)
		return;

	target.workers = new(std::nothrow) FlushWorker[count];
	if (target.workers == NULL)
		return;
	for (int32 i = 0; i < count; i++) {
		FlushWorker& worker = target.workers[i];
		worker.interface = this;
		worker.target = &target;
		worker.blank = 0;
		worker.shadow = 0;
		worker.start = create_sem(0, "vertical blank flush band");
		worker.thread = worker.start < 0 ? -1 : spawn_thread(
			_FlushWorkerEntry, "vertical blank flush band",
			B_REAL_TIME_DISPLAY_PRIORITY, &worker);
		if (worker.thread < 0) {
			if (worker.start >= 0)
				delete_sem(worker.start);
			break;
		}
		resume_thread(worker.thread);
		target.workerCount++;
	}
}


void
HWInterface::_StopFlushWorkers(FlushTarget& target)
{
	for (int32 i = 0; i < target.workerCount; i++) {
		FlushWorker& worker = target.workers[i];
		delete_sem(worker.start);
		status_t result;
		wait_for_thread(worker.thread, &result);
	}
	target.workerCount = 0;
	delete[] target.workers;
	target.workers = NULL;
}


/*!	Copies what is waiting for the target, in order from the top: a region's
	rectangles come sorted that way. A large flush is split into bands of
	rows, one per worker thread and one for this thread. When the blank is
	known, each copy is checked against where the scan should be by then (a
	frame's time over the output's rows, which leaves the blank's own lines
	as a margin): behind it at the start, the rows up to the scan still
	show the old picture; behind it at the end, the scan overtook the copy.
*/
/*!	The other shadow becomes the current one (the drawing ops' target),
	unless a target is still copying from it: then the generation stays.
	Returns the shadow to copy from. */
int32
HWInterface::_FlipShadow()
{
	if (!fFlushLock.Lock())
		return 0;
	// The shadow about to be written again must have been copied by every
	// target first: with two outputs flipping at their own blanks, a
	// target could otherwise skip a generation and copy it later, older
	// content over newer (trails of a moved window on the second display).
	bool consumed = fShadowReaders == 0;
	int32 old = fShadowCurrent ^ 1;
	for (int32 i = 0; consumed && i < fFlushTargetCount; i++) {
		if (fFlushTargets[i].pending[old].CountRects() > 0)
			consumed = false;
	}
	if (consumed)
		fShadowCurrent ^= 1;
	int32 which = fShadowCurrent ^ 1;
	fFlushLock.Unlock();
	return which;
}


void
HWInterface::_FlushPending(FlushTarget& target, bigtime_t blank, int32 which)
{
	BRegion pending;
	IntRect targetFrame;
	uint32 queued = 0;
	bool more = false;
	if (!fFlushLock.Lock())
		return;
	pending = target.pending[which];
	target.pending[which].MakeEmpty();
	more = target.pending[which ^ 1].CountRects() > 0;
	targetFrame = target.frame;
	queued = target.queued;
	if (pending.CountRects() > 0)
		fShadowReaders++;
	fFlushLock.Unlock();
	if (pending.CountRects() == 0) {
		if (more) {
			// what came since another target's flip: at the next blank
			release_sem_etc(target.wake, 1, B_DO_NOT_RESCHEDULE);
		}
		return;
	}

	// a drawing op that picked this shadow just before the flip may still
	// be writing it (the flip came early, so hardly ever): a memcpy
	bigtime_t waitBegan = system_time();
	for (bigtime_t deadline = 0; ; ) {
		bool writing = false;
		if (fFlushLock.Lock()) {
			for (int32 i = 0; i < 32; i++) {
				if (fShadowWrites[i].used && fShadowWrites[i].shadow == which)
					writing = true;
			}
			fFlushLock.Unlock();
		}
		if (!writing)
			break;
		if (deadline == 0)
			deadline = system_time() + 1500;
		else if (system_time() > deadline)
			break;
		snooze(20);
	}
	target.waitedWrites = system_time() - waitBegan;

	struct Reading {
		Reading(HWInterface* interface) : fInterface(interface) {}
		~Reading()
		{
			if (fInterface->fFlushLock.Lock()) {
				fInterface->fShadowReaders--;
				fInterface->fFlushLock.Unlock();
			}
		}
		HWInterface* fInterface;
	} reading(this);

	// whatever happens below, what was queued counts as dealt with
	struct Flushed {
		Flushed(HWInterface* interface, FlushTarget& target, uint32 queued)
			: fInterface(interface), fTarget(target), fQueued(queued) {}
		~Flushed()
		{
			if (fInterface->fFlushLock.Lock()) {
				fTarget.flushed = fQueued;
				fInterface->fFlushLock.Unlock();
			}
		}
		HWInterface* fInterface;
		FlushTarget& fTarget;
		uint32 fQueued;
	} flushed(this, target, queued);

	bigtime_t lockBegan = system_time();
	if (!LockParallelAccess())
		return;

	RenderingBuffer* backBuffer = BackBuffer();
	if (backBuffer == NULL || FrontBuffer() == NULL) {
		UnlockParallelAccess();
		return;
	}
	// the buffer may have changed since these were queued
	BRegion bufferRegion((BRect)IntRect(backBuffer->Bounds()));
	pending.IntersectWith(&bufferRegion);
	if (!targetFrame.IsValid())
		targetFrame = IntRect(backBuffer->Bounds());
	target.scanFrame = targetFrame;
	int32 rows = targetFrame.Height() + 1;
	bigtime_t period = target.period;
	bigtime_t start = system_time();
	float worst = 0;
	int64 late = 0;
	int32 count = pending.CountRects();

	bool cursorLocked = fFloatingOverlaysLock.Lock();
	target.waitedLocks = system_time() - lockBegan;
	IntRect frame(pending.Frame());
	if (IsDoubleBuffered())
		pending.Exclude((clipping_rect)_CursorFrame());

	// a band is worth a thread from about a 4K display's half
	const int64 kBandArea = 4096 * 1024;
	int64 area = 0;
	for (int32 i = 0; i < count; i++) {
		clipping_rect r = pending.RectAtInt(i);
		area += (int64)(r.right - r.left + 1) * (r.bottom - r.top + 1);
	}
	int32 bands = 1;
	if (area > kBandArea && (fDeviceCopyOff || !_DeviceCopiesShadows())) {
		if (target.workers == NULL)
			_StartFlushWorkers(target);
		if (target.workerCount > 0) {
			bands = min_c(target.workerCount + 1,
				(int32)(area / kBandArea) + 1);
		}
	}

	if (bands == 1) {
		// all the rects at once, in bands of rows across them (the copy
		// goes down the screen as the scan does)
		bool behind = false;
		if (blank != 0 && frame.top >= targetFrame.bottom - 120) {
			// all in the last rows: not before the previous frame's scan
			// has passed them (the flush begins a little early)
			while (system_time() < blank)
				;
		}
		float scanAtStart = _FlushScanRow(target, blank);
		if (scanAtStart > frame.top) {
			behind = true;
			worst = max_c(worst, (scanAtStart - frame.top) / rows);
		}
		bigtime_t copyStart = system_time();
		_CopyShadowToFront(pending, which);
		float scan = _FlushScanRow(target, blank);
		if (scan > frame.bottom + 1) {
			behind = true;
			worst = max_c(worst, (scan - frame.bottom - 1) / rows);
		}
		if (behind) {
			late++;
			if (target.lateReports < 3) {
				target.lateReports++;
				debug_printf("app_server: vertical blank flush %s: behind "
					"the scan: %" B_PRId32 " rects in %" B_PRId32 ",%"
					B_PRId32 "-%" B_PRId32 ",%" B_PRId32 ", scan at row %.0f "
					"when the copy began %.2f ms after the blank, at %.0f "
					"when it ended %.2f ms after (woke %.2f ms after it, "
					"waited %.2f ms for writes, %.2f for locks)\n",
					target.name, count, frame.left, frame.top, frame.right,
					frame.bottom, scanAtStart, (copyStart - blank) / 1000.0,
					scan, (system_time() - blank) / 1000.0,
					target.wokeLate / 1000.0, target.waitedWrites / 1000.0,
					target.waitedLocks / 1000.0);
			}
		}
	} else {
		// bands of equal height from the top; this thread takes the first
		int32 top = frame.top;
		int32 bandRows = (frame.Height() + 1 + bands - 1) / bands;
		BRegion first;
		for (int32 i = 0; i < bands; i++) {
			BRegion* band = i == 0 ? &first : &target.workers[i - 1].region;
			BRegion bandRegion(BRect(frame.left, top, frame.right,
				min_c(top + bandRows - 1, frame.bottom)));
			*band = pending;
			band->IntersectWith(&bandRegion);
			top += bandRows;
			if (i > 0) {
				target.workers[i - 1].blank = blank;
				target.workers[i - 1].shadow = which;
				release_sem_etc(target.workers[i - 1].start, 1, 0);
			}
		}
		bigtime_t began = system_time();
		float beganRow = _FlushScanRow(target, blank);
		_CopyShadowToFront(first, which);
		bigtime_t ended = system_time();
		float endedRow = _FlushScanRow(target, blank);
		for (int32 i = 1; i < bands; i++) {
			while (acquire_sem(fFlushDoneSem) == B_INTERRUPTED)
				;
		}

		for (int32 i = 0; i < bands; i++) {
			FlushWorker* worker = i == 0 ? NULL : &target.workers[i - 1];
			BRegion* band = i == 0 ? &first : &worker->region;
			if (band->CountRects() == 0)
				continue;
			IntRect bandFrame(band->Frame());
			float scan = i == 0 ? beganRow : worker->beganRow;
			bool behind = false;
			if (scan > bandFrame.top) {
				behind = true;
				worst = max_c(worst, (scan - bandFrame.top) / rows);
			}
			scan = i == 0 ? endedRow : worker->endedRow;
			if (scan > bandFrame.bottom + 1) {
				behind = true;
				worst = max_c(worst, (scan - bandFrame.bottom - 1) / rows);
			}
			if (behind) {
				late++;
				if (target.lateReports < 3) {
					target.lateReports++;
					debug_printf("app_server: vertical blank flush %s: behind "
						"the scan: band %" B_PRId32 " rows %" B_PRId32 "-%"
						B_PRId32 ", scan at row %.0f when its copy began "
						"%.2f ms after the blank, %.0f when it ended %.2f ms "
						"after (woke %.2f ms after it, waited %.2f ms for "
						"writes, %.2f for locks)\n", target.name,
						i, bandFrame.top, bandFrame.bottom,
						i == 0 ? beganRow : worker->beganRow,
						((i == 0 ? began : worker->began) - blank) / 1000.0,
						scan,
						((i == 0 ? ended : worker->ended) - blank) / 1000.0,
						target.wokeLate / 1000.0, target.waitedWrites / 1000.0,
						target.waitedLocks / 1000.0);
				}
			}
		}
	}

	_DrawCursor(frame);
	if (cursorLocked)
		fFloatingOverlaysLock.Unlock();

	UnlockParallelAccess();

	bigtime_t took = system_time() - start;
	target.flushes++;
	target.rects += count;
	target.late += late;
	target.worstLate = max_c(target.worstLate, worst);
	target.longest = max_c(target.longest, took);

	if (start - target.reported > 60000000) {
		if (target.reported != 0) {
			debug_printf("app_server: vertical blank flush %s: %" B_PRId64
				" flushes, %" B_PRId64 " rects, %" B_PRId64 " behind the scan"
				" (worst %.2f frames, scan %s), longest %.1f ms, period "
				"%.3f ms, %" B_PRId32 " bands\n", target.name,
				target.flushes, target.rects, target.late, target.worstLate,
				target.scanLineKnown == 1 ? "read from the hardware"
					: "estimated from the blank",
				target.longest / 1000.0, period / 1000.0, bands);
		}
		target.reported = start;
		target.flushes = 0;
		target.rects = 0;
		target.late = 0;
		target.lateReports = 0;
		target.worstLate = 0;
		target.longest = 0;
	}
}


status_t
HWInterface::CopyBackToFront(const BRegion& region)
{
	int32 count = region.CountRects();
	for (int32 i = 0; i < count; i++)
		CopyBackToFront(region.RectAt(i));
	return B_OK;
}


/*!	Waits until everything queued so far has been copied to the screen (or
	the timeout passes): for reading the screen back. Not to be called with
	the interface locked, since the flush threads need it.
*/
void
HWInterface::WaitForPendingFlush(bigtime_t timeout)
{
	if (!fFlushing || fFlushTargets == NULL)
		return;

	uint32 want[kMaxFlushTargets];
	int32 count = 0;
	if (!fFlushLock.Lock())
		return;
	count = fFlushTargetCount;
	for (int32 i = 0; i < count; i++)
		want[i] = fFlushTargets[i].queued;
	fFlushLock.Unlock();

	bigtime_t deadline = system_time() + timeout;
	while (true) {
		bool done = true;
		if (!fFlushLock.Lock())
			return;
		for (int32 i = 0; i < count; i++) {
			if ((int32)(fFlushTargets[i].flushed - want[i]) < 0)
				done = false;
		}
		fFlushLock.Unlock();
		if (done || system_time() >= deadline)
			return;
		snooze(1000);
	}
}


// #pragma mark -


overlay_token
HWInterface::AcquireOverlayChannel()
{
	return NULL;
}


void
HWInterface::ReleaseOverlayChannel(overlay_token token)
{
}


status_t
HWInterface::GetOverlayRestrictions(const Overlay* overlay,
	overlay_restrictions* restrictions)
{
	return B_NOT_SUPPORTED;
}


bool
HWInterface::CheckOverlayRestrictions(int32 width, int32 height,
	color_space colorSpace)
{
	return false;
}


const overlay_buffer*
HWInterface::AllocateOverlayBuffer(int32 width, int32 height, color_space space)
{
	return NULL;
}


void
HWInterface::FreeOverlayBuffer(const overlay_buffer* buffer)
{
}


void
HWInterface::ConfigureOverlay(Overlay* overlay)
{
}


void
HWInterface::HideOverlay(Overlay* overlay)
{
}


// #pragma mark -


bool
HWInterface::HideFloatingOverlays(const BRect& area)
{
	if (IsDoubleBuffered())
		return false;
	if (!fFloatingOverlaysLock.Lock())
		return false;
	if (fCursorAreaBackup.IsSet() && !fCursorAreaBackup->cursor_hidden) {
		BRect backupArea(fCursorAreaBackup->left, fCursorAreaBackup->top,
			fCursorAreaBackup->right, fCursorAreaBackup->bottom);
		if (area.Intersects(backupArea)) {
			_RestoreCursorArea();
			// do not unlock the cursor lock
			return true;
		}
	}
	fFloatingOverlaysLock.Unlock();
	return false;
}


bool
HWInterface::HideFloatingOverlays()
{
	if (IsDoubleBuffered())
		return false;
	if (!fFloatingOverlaysLock.Lock())
		return false;

	_RestoreCursorArea();
	return true;
}


void
HWInterface::ShowFloatingOverlays()
{
	if (fCursorAreaBackup.IsSet() && fCursorAreaBackup->cursor_hidden)
		_DrawCursor(_CursorFrame());

	fFloatingOverlaysLock.Unlock();
}


// #pragma mark -


bool
HWInterface::AddListener(HWInterfaceListener* listener)
{
	if (listener && !fListeners.HasItem(listener))
		return fListeners.AddItem(listener);
	return false;
}


void
HWInterface::RemoveListener(HWInterfaceListener* listener)
{
	fListeners.RemoveItem(listener);
}


// #pragma mark -


/*!	Default implementation, can be used as fallback or for software cursor.
	\param area is where we potentially draw the cursor, the cursor
		might be somewhere else, in which case this function does nothing
*/
void
HWInterface::_DrawCursor(IntRect area) const
{
	RenderingBuffer* backBuffer = DrawingBuffer();
	if (!backBuffer || !area.IsValid())
		return;

	IntRect cf = _CursorFrame();

	// make sure we don't copy out of bounds
	area = backBuffer->Bounds() & area;

	if (cf.IsValid() && area.Intersects(cf)) {

		// clip to common area
		area = area & cf;

		int32 width = area.right - area.left + 1;
		int32 height = area.bottom - area.top + 1;

		// make a bitmap from the backbuffer
		// that has the cursor blended on top of it

		// blending buffer
		uint8* buffer = new(std::nothrow) uint8[width * height * 4];
			// TODO: cache this buffer
		if (buffer == NULL)
			return;

		// offset into back buffer
		uint8* src = (uint8*)backBuffer->Bits();
		uint32 srcBPR = backBuffer->BytesPerRow();
		src += area.top * srcBPR + area.left * 4;

		// offset into cursor bitmap
		uint8* crs = (uint8*)fCursorAndDragBitmap->Bits();
		uint32 crsBPR = fCursorAndDragBitmap->BytesPerRow();
		// since area is clipped to cf,
		// the diff between area.top and cf.top is always positive,
		// same for diff between area.left and cf.left
		crs += (area.top - (int32)floorf(cf.top)) * crsBPR
				+ (area.left - (int32)floorf(cf.left)) * 4;

		uint8* dst = buffer;

		if (fCursorAreaBackup.IsSet() && fCursorAreaBackup->buffer
			&& fFloatingOverlaysLock.Lock()) {
			fCursorAreaBackup->cursor_hidden = false;
			// remember which area the backup contains
			fCursorAreaBackup->left = area.left;
			fCursorAreaBackup->top = area.top;
			fCursorAreaBackup->right = area.right;
			fCursorAreaBackup->bottom = area.bottom;
			uint8* bup = fCursorAreaBackup->buffer;
			uint32 bupBPR = fCursorAreaBackup->bpr;

			// blending and backup of drawing buffer
			for (int32 y = area.top; y <= area.bottom; y++) {
				uint8* s = src;
				uint8* c = crs;
				uint8* d = dst;
				uint8* b = bup;

				for (int32 x = area.left; x <= area.right; x++) {
					*(uint32*)b = *(uint32*)s;
					// assumes backbuffer alpha = 255
					// assuming pre-multiplied cursor bitmap
					int a = 255 - c[3];
					d[0] = ((int)(b[0] * a + 255) >> 8) + c[0];
					d[1] = ((int)(b[1] * a + 255) >> 8) + c[1];
					d[2] = ((int)(b[2] * a + 255) >> 8) + c[2];

					s += 4;
					c += 4;
					d += 4;
					b += 4;
				}
				crs += crsBPR;
				src += srcBPR;
				dst += width * 4;
				bup += bupBPR;
			}
			fFloatingOverlaysLock.Unlock();
		} else {
			// blending
			for (int32 y = area.top; y <= area.bottom; y++) {
				uint8* s = src;
				uint8* c = crs;
				uint8* d = dst;
				for (int32 x = area.left; x <= area.right; x++) {
					// assumes backbuffer alpha = 255
					// assuming pre-multiplied cursor bitmap
					uint8 a = 255 - c[3];
					d[0] = ((s[0] * a + 255) >> 8) + c[0];
					d[1] = ((s[1] * a + 255) >> 8) + c[1];
					d[2] = ((s[2] * a + 255) >> 8) + c[2];

					s += 4;
					c += 4;
					d += 4;
				}
				crs += crsBPR;
				src += srcBPR;
				dst += width * 4;
			}
		}
		// copy result to front buffer
		_CopyToFront(buffer, width * 4, area.left, area.top, area.right,
			area.bottom);

		delete[] buffer;
	}
}


/*!	- source is assumed to be already at the right offset
	- source is assumed to be in B_RGBA32 format
	- location in front buffer is calculated
	- conversion from B_RGBA32 to format of front buffer is taken care of
*/
/*!	Enlarges the logical rectangle \a x, \a y - \a right, \a bottom of the
	B_RGBA32 source onto the front buffer, by the software scale. Whole
	scales duplicate pixels, which keeps text crisp; fractional ones
	interpolate. Neighbours outside the source rectangle are taken from
	its edge, which is right for the cursor's private buffer and close
	enough for a partial update of the back buffer.
*/
void
HWInterface::_CopyToFrontScaled(uint8* src, uint32 srcBPR, int32 x, int32 y,
	int32 right, int32 bottom) const
{
	RenderingBuffer* frontBuffer = FrontBuffer();
	RenderingBuffer* backBuffer = BackBuffer();
	// from back buffer pixels to front buffer pixels
	const float scale = (float)frontBuffer->Width() / backBuffer->Width();
	const bool whole = fabsf(scale - roundf(scale)) < 0.001f;

	int32 left = (int32)floorf(x * scale);
	int32 top = (int32)floorf(y * scale);
	int32 physicalRight = (int32)ceilf((right + 1) * scale) - 1;
	int32 physicalBottom = (int32)ceilf((bottom + 1) * scale) - 1;
	if (left < 0)
		left = 0;
	if (top < 0)
		top = 0;
	if (physicalRight > (int32)frontBuffer->Width() - 1)
		physicalRight = frontBuffer->Width() - 1;
	if (physicalBottom > (int32)frontBuffer->Height() - 1)
		physicalBottom = frontBuffer->Height() - 1;
	if (left > physicalRight || top > physicalBottom)
		return;

	const int32 width = physicalRight - left + 1;
	const int32 sourceWidth = right - x + 1;
	const int32 sourceHeight = bottom - y + 1;

	uint8* row = new(std::nothrow) uint8[width * 4];
	int32* column = new(std::nothrow) int32[width];
	uint16* columnWeight = new(std::nothrow) uint16[width];
	if (row == NULL || column == NULL || columnWeight == NULL) {
		delete[] row;
		delete[] column;
		delete[] columnWeight;
		return;
	}

	// where each output column samples the source, relative to the source
	// rectangle, and how far it is towards the next column (in 1/256)
	for (int32 i = 0; i < width; i++) {
		float sourceX = whole ? (left + i) / scale
			: (left + i + 0.5f) / scale - 0.5f;
		float relative = sourceX - x;
		int32 index = (int32)floorf(relative);
		int32 weight = whole ? 0 : (int32)((relative - index) * 256);
		if (index < 0) {
			index = 0;
			weight = 0;
		}
		if (index >= sourceWidth - 1) {
			index = sourceWidth - 1;
			weight = 0;
		}
		column[i] = index;
		columnWeight[i] = weight;
	}

	for (int32 py = top; py <= physicalBottom; py++) {
		float sourceY = whole ? py / scale : (py + 0.5f) / scale - 0.5f;
		float relative = sourceY - y;
		int32 rowIndex = (int32)floorf(relative);
		int32 rowWeight = whole ? 0 : (int32)((relative - rowIndex) * 256);
		if (rowIndex < 0) {
			rowIndex = 0;
			rowWeight = 0;
		}
		if (rowIndex >= sourceHeight - 1) {
			rowIndex = sourceHeight - 1;
			rowWeight = 0;
		}
		const uint8* line0 = src + rowIndex * srcBPR;
		const uint8* line1 = rowWeight != 0 ? line0 + srcBPR : line0;

		uint8* out = row;
		for (int32 i = 0; i < width; i++) {
			const uint8* p00 = line0 + column[i] * 4;
			int32 wx = columnWeight[i];
			if (wx == 0 && rowWeight == 0) {
				out[0] = p00[0];
				out[1] = p00[1];
				out[2] = p00[2];
				out[3] = 255;
			} else {
				const uint8* p01 = wx != 0 ? p00 + 4 : p00;
				const uint8* p10 = line1 + column[i] * 4;
				const uint8* p11 = wx != 0 ? p10 + 4 : p10;
				for (int32 c = 0; c < 3; c++) {
					int32 top = p00[c] * (256 - wx) + p01[c] * wx;
					int32 bottom = p10[c] * (256 - wx) + p11[c] * wx;
					out[c] = (uint8)((top * (256 - rowWeight)
						+ bottom * rowWeight) >> 16);
				}
				out[3] = 255;
			}
			out += 4;
		}
		_CopyRowToFront(row, left, py, width);
	}

	delete[] row;
	delete[] column;
	delete[] columnWeight;
}


/*!	One row of B_RGBA32 pixels to the front buffer at \a x, \a y, converted
	to its color space.
*/
void
HWInterface::_CopyRowToFront(const uint8* row, int32 x, int32 y,
	int32 count) const
{
	RenderingBuffer* frontBuffer = FrontBuffer();
	uint8* dst = (uint8*)frontBuffer->Bits();
	uint32 dstBPR = frontBuffer->BytesPerRow();
	const uint8* srcHandle = row;

	switch (frontBuffer->ColorSpace()) {
		case B_RGB32:
		case B_RGBA32:
			memcpy(dst + y * dstBPR + x * 4, row, count * 4);
			break;

		case B_RGB30:
		{
			uint32* dstHandle = (uint32*)(dst + y * dstBPR + x * 4);
			for (int32 i = 0; i < count; i++, srcHandle += 4) {
				uint32 r = srcHandle[0];
				uint32 g = srcHandle[1];
				uint32 b = srcHandle[2];
				*dstHandle++ = ((r * 1023) / 255) | (((g * 1023) / 255) << 10)
					| (((b * 1023) / 255) << 20);
			}
			break;
		}

		case B_RGB24:
		{
			uint8* dstHandle = dst + y * dstBPR + x * 3;
			for (int32 i = 0; i < count; i++, srcHandle += 4) {
				dstHandle[0] = srcHandle[0];
				dstHandle[1] = srcHandle[1];
				dstHandle[2] = srcHandle[2];
				dstHandle += 3;
			}
			break;
		}

		case B_RGB16:
		{
			uint16* dstHandle = (uint16*)(dst + y * dstBPR + x * 2);
			for (int32 i = 0; i < count; i++, srcHandle += 4) {
				*dstHandle++ = (uint16)(((srcHandle[2] & 0xf8) << 8)
					| ((srcHandle[1] & 0xfc) << 3) | (srcHandle[0] >> 3));
			}
			break;
		}

		case B_RGB15:
		case B_RGBA15:
		{
			uint16* dstHandle = (uint16*)(dst + y * dstBPR + x * 2);
			for (int32 i = 0; i < count; i++, srcHandle += 4) {
				*dstHandle++ = (uint16)(((srcHandle[2] & 0xf8) << 7)
					| ((srcHandle[1] & 0xf8) << 2) | (srcHandle[0] >> 3));
			}
			break;
		}

		case B_CMAP8:
		{
			const color_map* colorMap = SystemColorMap();
			uint8* dstHandle = dst + y * dstBPR + x;
			for (int32 i = 0; i < count; i++, srcHandle += 4) {
				uint16 index = ((srcHandle[2] & 0xf8) << 7)
					| ((srcHandle[1] & 0xf8) << 2) | (srcHandle[0] >> 3);
				*dstHandle++ = colorMap->index_map[index];
			}
			break;
		}

		default:
			break;
	}
}


void
HWInterface::_CopyToFront(uint8* src, uint32 srcBPR, int32 x, int32 y,
	int32 right, int32 bottom) const
{
	RenderingBuffer* frontBuffer = FrontBuffer();
	RenderingBuffer* backBuffer = BackBuffer();
	if (backBuffer != NULL && (backBuffer->Width() != frontBuffer->Width()
			|| backBuffer->Height() != frontBuffer->Height())) {
		_CopyToFrontScaled(src, srcBPR, x, y, right, bottom);
		return;
	}

	_CopyToBuffer(frontBuffer, src, srcBPR, x, y, right, bottom);
}


void
HWInterface::_CopyToBuffer(RenderingBuffer* buffer, uint8* src,
	uint32 srcBPR, int32 x, int32 y, int32 right, int32 bottom) const
{
	if (buffer == NULL)
		return;

	uint8* dst = (uint8*)buffer->Bits();
	uint32 dstBPR = buffer->BytesPerRow();

	// transfer, handle colorspace conversion
	switch (buffer->ColorSpace()) {
		case B_RGB32:
		case B_RGBA32:
		{
			int32 bytes = (right - x + 1) * 4;

			if (bytes > 0) {
				// offset to left top pixel in dest buffer
				dst += y * dstBPR + x * 4;
				// copy
				for (; y <= bottom; y++) {
					// bytes is guaranteed to be multiple of 4
					memcpy(dst, src, bytes);
					dst += dstBPR;
					src += srcBPR;
				}
			}
			break;
		}

		case B_RGB30:
		{
			dst += y * dstBPR + x * 4;
			for (; y <= bottom; y++) {
				uint32* srcHandle = (uint32*)dst;
				uint32* dstHandle = (uint32*)src;
				for (int32 left = x; left <= right; left++, srcHandle++, dstHandle++) {
					uint32 r = (*dstHandle) & 0xff;
					uint32 g = (*dstHandle >> 8) & 0xff;
					uint32 b = (*dstHandle >> 16) & 0xff;
					*srcHandle = ((r * 1023) / 255)
						| (((g * 1023) / 255) << 10)
						| (((b * 1023) / 255) << 20);
				}

				src += srcBPR;
				dst += dstBPR;
			}
			break;
		}

		case B_RGB24:
		{
			// offset to left top pixel in dest buffer
			dst += y * dstBPR + x * 3;
			int32 left = x;
			// copy
			for (; y <= bottom; y++) {
				uint8* srcHandle = src;
				uint8* dstHandle = dst;
				for (x = left; x <= right; x++) {
					dstHandle[0] = srcHandle[0];
					dstHandle[1] = srcHandle[1];
					dstHandle[2] = srcHandle[2];
					dstHandle += 3;
					srcHandle += 4;
				}
				dst += dstBPR;
				src += srcBPR;
			}
			break;
		}

		case B_RGB16:
		{
			// offset to left top pixel in dest buffer
			dst += y * dstBPR + x * 2;
			int32 left = x;
			// copy
			// TODO: assumes BGR order, does this work on big endian as well?
			for (; y <= bottom; y++) {
				uint8* srcHandle = src;
				uint16* dstHandle = (uint16*)dst;
				for (x = left; x <= right; x++) {
					*dstHandle = (uint16)(((srcHandle[2] & 0xf8) << 8)
						| ((srcHandle[1] & 0xfc) << 3) | (srcHandle[0] >> 3));
					dstHandle ++;
					srcHandle += 4;
				}
				dst += dstBPR;
				src += srcBPR;
			}
			break;
		}

		case B_RGB15:
		case B_RGBA15:
		{
			// offset to left top pixel in dest buffer
			dst += y * dstBPR + x * 2;
			int32 left = x;
			// copy
			// TODO: assumes BGR order, does this work on big endian as well?
			for (; y <= bottom; y++) {
				uint8* srcHandle = src;
				uint16* dstHandle = (uint16*)dst;
				for (x = left; x <= right; x++) {
					*dstHandle = (uint16)(((srcHandle[2] & 0xf8) << 7)
						| ((srcHandle[1] & 0xf8) << 2) | (srcHandle[0] >> 3));
					dstHandle ++;
					srcHandle += 4;
				}
				dst += dstBPR;
				src += srcBPR;
			}
			break;
		}

		case B_CMAP8:
		{
			const color_map *colorMap = SystemColorMap();
			// offset to left top pixel in dest buffer
			dst += y * dstBPR + x;
			int32 left = x;
			uint16 index;
			// copy
			// TODO: assumes BGR order again
			for (; y <= bottom; y++) {
				uint8* srcHandle = src;
				uint8* dstHandle = dst;
				for (x = left; x <= right; x++) {
					index = ((srcHandle[2] & 0xf8) << 7)
						| ((srcHandle[1] & 0xf8) << 2) | (srcHandle[0] >> 3);
					*dstHandle = colorMap->index_map[index];
					dstHandle ++;
					srcHandle += 4;
				}
				dst += dstBPR;
				src += srcBPR;
			}

			break;
		}

		case B_GRAY8:
			if (buffer->Width() > dstBPR) {
				// VGA 16 color grayscale planar mode
				if (fVGADevice >= 0) {
					vga_planar_blit_args args;
					args.source = src;
					args.source_bytes_per_row = srcBPR;
					args.left = x;
					args.top = y;
					args.right = right;
					args.bottom = bottom;
					if (ioctl(fVGADevice, VGA_PLANAR_BLIT, &args, sizeof(args))
							== 0)
						break;
				}

				// Since we cannot set the plane, we do monochrome output
				dst += y * dstBPR + x / 8;
				int32 left = x;

				// TODO: this is awfully slow...
				// TODO: assumes BGR order
				for (; y <= bottom; y++) {
					uint8* srcHandle = src;
					uint8* dstHandle = dst;
					uint8 current8 = dstHandle[0];
						// we store 8 pixels before writing them back

					for (x = left; x <= right; x++) {
						uint8 pixel = (308 * srcHandle[2] + 600 * srcHandle[1]
							+ 116 * srcHandle[0]) / 1024;
						srcHandle += 4;

						if (pixel > 128)
							current8 |= 0x80 >> (x & 7);
						else
							current8 &= ~(0x80 >> (x & 7));

						if ((x & 7) == 7) {
							// last pixel in 8 pixel group
							dstHandle[0] = current8;
							dstHandle++;
							current8 = dstHandle[0];
						}
					}

					if (x & 7) {
						// last pixel has not been written yet
						dstHandle[0] = current8;
					}
					dst += dstBPR;
					src += srcBPR;
				}
			} else {
				// offset to left top pixel in dest buffer
				dst += y * dstBPR + x;
				int32 left = x;
				// copy
				// TODO: assumes BGR order, does this work on big endian as well?
				for (; y <= bottom; y++) {
					uint8* srcHandle = src;
					uint8* dstHandle = dst;
					for (x = left; x <= right; x++) {
						*dstHandle = (308 * srcHandle[2] + 600 * srcHandle[1]
							+ 116 * srcHandle[0]) / 1024;
						dstHandle ++;
						srcHandle += 4;
					}
					dst += dstBPR;
					src += srcBPR;
				}
			}
			break;

		default:
			fprintf(stderr, "HWInterface::CopyBackToFront() - unsupported "
				"front buffer format! (0x%x)\n", buffer->ColorSpace());
			break;
	}
}


/*!	The object must be locked
*/
IntRect
HWInterface::_CursorFrame() const
{
	IntRect frame(0, 0, -1, -1);
	if (fCursorAndDragBitmap && fCursorVisible && !fHardwareCursorEnabled) {
		frame = fCursorAndDragBitmap->Bounds();
		// the cursor location is logical, the bitmap is in buffer pixels
		float factor = RenderScaleFactor();
		BPoint location(floorf(fCursorLocation.x * factor),
			floorf(fCursorLocation.y * factor));
		frame.OffsetTo(location - fCursorAndDragBitmap->GetHotSpot());
	}
	return frame;
}


/*!	A copy of \a cursor with fRenderScale pixels per pixel of the density it
	was made for, by duplicating (or dropping) pixels. System cursors are
	rendered at the right density to begin with; this is for the cursors
	programs bring along.
*/
ServerCursor*
HWInterface::_CursorAtRenderScale(ServerCursor* cursor)
{
	float from = cursor->PixelScale() < 1 ? 1 : cursor->PixelScale();
	float to = RenderScaleFactor();
	int32 width = (int32)roundf(cursor->Width() * to / from);
	int32 height = (int32)roundf(cursor->Height() * to / from);
	if (width < 1 || height < 1 || cursor->ColorSpace() != B_RGBA32) {
		ServerCursor* copy = new(std::nothrow) ServerCursor(cursor);
		if (copy != NULL)
			copy->SetPixelScale(to);
		return copy;
	}

	BPoint hotSpot = cursor->GetHotSpot();
	hotSpot.x = floorf(hotSpot.x * to / from);
	hotSpot.y = floorf(hotSpot.y * to / from);
	ServerCursor* scaled = new(std::nothrow) ServerCursor(
		BRect(0, 0, width - 1, height - 1), B_RGBA32, 0, hotSpot);
	if (scaled == NULL || scaled->Bits() == NULL) {
		delete scaled;
		return new(std::nothrow) ServerCursor(cursor);
	}
	scaled->SetPixelScale(to);

	const uint8* src = (const uint8*)cursor->Bits();
	uint32 srcBPR = cursor->BytesPerRow();
	uint8* dst = (uint8*)scaled->Bits();
	uint32 dstBPR = scaled->BytesPerRow();
	int32 srcWidth = cursor->Width();
	int32 srcHeight = cursor->Height();
	for (int32 y = 0; y < height; y++) {
		int32 sy = min_c((int32)(y * from / to), srcHeight - 1);
		const uint32* srcRow = (const uint32*)(src + sy * srcBPR);
		uint32* dstRow = (uint32*)(dst + y * dstBPR);
		for (int32 x = 0; x < width; x++)
			dstRow[x] = srcRow[min_c((int32)(x * from / to), srcWidth - 1)];
	}
	return scaled;
}


void
HWInterface::_RestoreCursorArea() const
{
	if (fCursorAreaBackup.IsSet() && !fCursorAreaBackup->cursor_hidden) {
		_CopyToFront(fCursorAreaBackup->buffer, fCursorAreaBackup->bpr,
			fCursorAreaBackup->left, fCursorAreaBackup->top,
			fCursorAreaBackup->right, fCursorAreaBackup->bottom);

		fCursorAreaBackup->cursor_hidden = true;
	}
}


void
HWInterface::_AdoptDragBitmap()
{
	// TODO: support other colorspaces/convert bitmap
	if (fDragBitmap && !(fDragBitmap->ColorSpace() == B_RGB32
		|| fDragBitmap->ColorSpace() == B_RGBA32)) {
		fprintf(stderr, "HWInterface::_AdoptDragBitmap() - bitmap has yet "
			"unsupported colorspace\n");
		return;
	}

	_RestoreCursorArea();
	BRect oldCursorFrame = _CursorFrame();

	if (fDragBitmap != NULL && fDragBitmap->Bounds().Width() > 0 && fDragBitmap->Bounds().Height() > 0) {
		// the drag bitmap and its offset are logical; everything here is in
		// buffer pixels
		float factor = RenderScaleFactor();
		BRect bitmapFrame = fDragBitmap->Bounds();
		bitmapFrame.right = roundf((bitmapFrame.right + 1) * factor) - 1;
		bitmapFrame.bottom = roundf((bitmapFrame.bottom + 1) * factor) - 1;
		BPoint dragOffset(floorf(fDragBitmapOffset.x * factor),
			floorf(fDragBitmapOffset.y * factor));
		if (fCursor) {
			// put bitmap frame and cursor frame into the same
			// coordinate space (the cursor location is the origin)
			bitmapFrame.OffsetTo(BPoint(-dragOffset.x, -dragOffset.y));

			BRect cursorFrame(fCursor->Bounds());
			BPoint hotspot(fCursor->GetHotSpot());
				// the hotspot is at the origin
			cursorFrame.OffsetTo(-hotspot.x, -hotspot.y);

			BRect combindedBounds = bitmapFrame | cursorFrame;

			BPoint shift;
			shift.x = -combindedBounds.left;
			shift.y = -combindedBounds.top;

			combindedBounds.OffsetBy(shift);
			cursorFrame.OffsetBy(shift);
			bitmapFrame.OffsetBy(shift);

			fCursorAndDragBitmap.SetTo(new(std::nothrow) ServerCursor(combindedBounds,
				fDragBitmap->ColorSpace(), 0, shift), true);

			uint8* dst = fCursorAndDragBitmap ? (uint8*)fCursorAndDragBitmap->Bits() : NULL;
			if (dst == NULL) {
				// Oops, we could not allocate memory for the drag bitmap.
				// Let's show the cursor only.
				fCursorAndDragBitmap = fCursor;
			} else {
				// clear the combined buffer
				uint32 dstBPR = fCursorAndDragBitmap->BytesPerRow();

				memset(dst, 0, fCursorAndDragBitmap->BitsLength());

				// put drag bitmap into combined buffer, enlarged to the
				// buffer's density
				uint8* src = (uint8*)fDragBitmap->Bits();
				uint32 srcBPR = fDragBitmap->BytesPerRow();

				dst += (int32)bitmapFrame.top * dstBPR
					+ (int32)bitmapFrame.left * 4;

				uint32 width = bitmapFrame.IntegerWidth() + 1;
				uint32 height = bitmapFrame.IntegerHeight() + 1;

				uint32 srcWidth = fDragBitmap->Width();
				uint32 srcHeight = fDragBitmap->Height();
				for (uint32 y = 0; y < height; y++) {
					uint32 sy = min_c((uint32)(y / factor), srcHeight - 1);
					const uint32* s = (const uint32*)(src + sy * srcBPR);
					uint32* d = (uint32*)dst;
					for (uint32 x = 0; x < width; x++)
						d[x] = s[min_c((uint32)(x / factor), srcWidth - 1)];
					dst += dstBPR;
				}

				// compose cursor into combined buffer
				dst = (uint8*)fCursorAndDragBitmap->Bits();
				dst += (int32)cursorFrame.top * dstBPR
					+ (int32)cursorFrame.left * 4;

				src = (uint8*)fCursor->Bits();
				srcBPR = fCursor->BytesPerRow();

				width = cursorFrame.IntegerWidth() + 1;
				height = cursorFrame.IntegerHeight() + 1;

				for (uint32 y = 0; y < height; y++) {
					uint8* d = dst;
					uint8* s = src;
					for (uint32 x = 0; x < width; x++) {
						// takes two semi-transparent pixels
						// with unassociated alpha (not pre-multiplied)
						// and stays within non-premultiplied color space
						if (s[3] > 0) {
							if (s[3] == 255) {
								d[0] = s[0];
								d[1] = s[1];
								d[2] = s[2];
								d[3] = 255;
							} else {
								uint8 alphaRest = 255 - s[3];
								uint32 alphaTemp
									= (65025 - alphaRest * (255 - d[3]));
								uint32 alphaDest = d[3] * alphaRest;
								uint32 alphaSrc = 255 * s[3];
								d[0] = (d[0] * alphaDest + s[0] * alphaSrc)
									/ alphaTemp;
								d[1] = (d[1] * alphaDest + s[1] * alphaSrc)
									/ alphaTemp;
								d[2] = (d[2] * alphaDest + s[2] * alphaSrc)
									/ alphaTemp;
								d[3] = alphaTemp / 255;
							}
						}
						// TODO: make sure the alpha is always upside down,
						// then it doesn't need to be done when drawing the cursor
						// (see _DrawCursor())
						//					d[3] = 255 - d[3];
						d += 4;
						s += 4;
					}
					dst += dstBPR;
					src += srcBPR;
				}

				// handle pre-multiplication with alpha
				// for faster compositing during cursor drawing
				width = combindedBounds.IntegerWidth() + 1;
				height = combindedBounds.IntegerHeight() + 1;

				dst = (uint8*)fCursorAndDragBitmap->Bits();

				for (uint32 y = 0; y < height; y++) {
					uint8* d = dst;
					for (uint32 x = 0; x < width; x++) {
						d[0] = (d[0] * d[3]) >> 8;
						d[1] = (d[1] * d[3]) >> 8;
						d[2] = (d[2] * d[3]) >> 8;
						d += 4;
					}
					dst += dstBPR;
				}
			}
		} else {
			ServerCursor* dragCursor = new(std::nothrow) ServerCursor(
				fDragBitmap->Bits(), fDragBitmap->Width(),
				fDragBitmap->Height(), fDragBitmap->ColorSpace());
			if (dragCursor != NULL && fRenderScale != 100) {
				ServerCursor* scaled = _CursorAtRenderScale(dragCursor);
				delete dragCursor;
				dragCursor = scaled;
			}
			fCursorAndDragBitmap.SetTo(dragCursor, true);
			if (fCursorAndDragBitmap)
				fCursorAndDragBitmap->SetHotSpot(BPoint(-dragOffset.x, -dragOffset.y));
		}
	} else {
		fCursorAndDragBitmap = fCursor;
	}

	Invalidate(oldCursorFrame);

	fCursorAreaBackup.Unset();

	if (!fCursorAndDragBitmap)
		return;

	if (fCursorAndDragBitmap && !IsDoubleBuffered()) {
		BRect cursorBounds = fCursorAndDragBitmap->Bounds();
		fCursorAreaBackup.SetTo(new buffer_clip(cursorBounds.IntegerWidth() + 1,
			cursorBounds.IntegerHeight() + 1));
		if (fCursorAreaBackup->buffer == NULL)
			fCursorAreaBackup.Unset();
	}
 	_DrawCursor(_CursorFrame());
}


void
HWInterface::_NotifyFrameBufferChanged()
{
	BList listeners(fListeners);
	int32 count = listeners.CountItems();
	for (int32 i = 0; i < count; i++) {
		HWInterfaceListener* listener
			= (HWInterfaceListener*)listeners.ItemAtFast(i);
		listener->FrameBufferChanged();
	}
}


void
HWInterface::_NotifyScreenChanged()
{
	BList listeners(fListeners);
	int32 count = listeners.CountItems();
	for (int32 i = 0; i < count; i++) {
		HWInterfaceListener* listener
			= (HWInterfaceListener*)listeners.ItemAtFast(i);
		listener->ScreenChanged(this);
	}
}


void
HWInterface::_NotifyFrameCommitted(const BRect& frame)
{
	BList listeners(fListeners);
	int32 count = listeners.CountItems();
	for (int32 i = 0; i < count; i++) {
		HWInterfaceListener* listener
			= (HWInterfaceListener*)listeners.ItemAtFast(i);
		listener->FrameCommitted(this, frame);
	}
}


/*static*/ bool
HWInterface::_IsValidMode(const display_mode& mode)
{
	// TODO: more of those!
	if (mode.virtual_width < 320
		|| mode.virtual_height < 200)
		return false;

	return true;
}

