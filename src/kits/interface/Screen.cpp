/*
 * Copyright 2003-2009, Haiku Inc.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 *		Stefano Ceccherini (burton666@libero.it)
 *		Axel Dörfler, axeld@pinc-software.de
 */


#include <Screen.h>

#include <pthread.h>

#include <Window.h>

#include <ApplicationPrivate.h>
#include <PrivateScreen.h>
#include <ServerReadOnlyMemory.h>


using namespace BPrivate;


BScreen::BScreen(screen_id id)
{
	fScreen = BPrivateScreen::Get(id.id);
}


BScreen::BScreen(BWindow* window)
{
	fScreen = BPrivateScreen::Get(window);
}


BScreen::~BScreen()
{
	BPrivateScreen::Put(fScreen);
}


bool
BScreen::IsValid()
{
	return fScreen != NULL && fScreen->IsValid();
}


status_t
BScreen::SetToNext()
{
	if (fScreen != NULL) {
		BPrivateScreen* screen = BPrivateScreen::GetNext(fScreen);
		if (screen != NULL) {
			fScreen = screen;
			return B_OK;
		}
	}
	return B_ERROR;
}


color_space
BScreen::ColorSpace()
{
	if (fScreen != NULL)
		return fScreen->ColorSpace();

	return B_NO_COLOR_SPACE;
}


BRect
BScreen::Frame()
{
	if (fScreen != NULL)
		return fScreen->Frame();

	return BRect(0, 0, 0, 0);
}


/*!	Returns how many frame buffer pixels make up one coordinate unit on the
	screen: 1 normally, 1.5 when the desktop is drawn at 150 percent for a
	high density monitor. Programs that render pixels themselves can render
	that many times as many and draw them with B_DEVICE_PIXEL_BITMAP, which
	keeps them sharp. B_SCREEN_CHANGED is sent to the windows when it
	changes.
*/
float
BScreen::DeviceScale()
{
	const server_read_only_memory* memory
		= BApplication::Private::ServerReadOnlyMemory();
	if (memory == NULL || !(memory->device_scale >= 1.0f))
		return 1.0f;
	return memory->device_scale;
}


screen_id
BScreen::ID()
{
	if (fScreen != NULL) {
		screen_id id = { fScreen->ID() };
		return id;
	}

	return B_MAIN_SCREEN_ID;
}


status_t
BScreen::WaitForRetrace()
{
	return WaitForRetrace(B_INFINITE_TIMEOUT);
}


status_t
BScreen::WaitForRetrace(bigtime_t timeout)
{
	if (fScreen != NULL)
		return fScreen->WaitForRetrace(timeout);

	return B_ERROR;
}


/*!	Waits for the next frame of the display output that shows \a frame (in
	screen coordinates; the output with the most of it, as GetScanTiming()
	picks it), and returns when its scan begins. WaitForRetrace() waits for
	the main output's blank, which on a desktop of several monitors is not
	the refresh of the one a window is on.
	Without scan timing for that output (no blanks), this is
	WaitForRetrace(). B_WOULD_BLOCK for a \a timeout of 0 and B_TIMED_OUT
	for one that ends before the frame begins, as for a semaphore.
*/
status_t
BScreen::WaitForOutputRetrace(BRect frame, bigtime_t timeout)
{
	// The last answer is used for half a second: asking app_server takes a
	// round trip, and on a quiet desktop app_server first waits for a blank
	// itself (up to two frames), more than a short timeout allows. A
	// prediction drifts by some 70 us a second (the period is in whole
	// microseconds).
	static pthread_mutex_t sLock = PTHREAD_MUTEX_INITIALIZER;
	static display_scan_timing sTiming;
	static BRect sFrame;
	static int32 sScreen = -1;
	static bigtime_t sAsked = 0;
	const bigtime_t kKeepTiming = 500000;

	display_scan_timing timing;
	bool known = false;
	pthread_mutex_lock(&sLock);
	if (sScreen == ID().id && sFrame == frame
		&& system_time() - sAsked < kKeepTiming) {
		timing = sTiming;
		known = true;
	}
	pthread_mutex_unlock(&sLock);

	if (!known) {
		if (GetScanTiming(frame, &timing) != B_OK)
			return WaitForRetrace(timeout);
		pthread_mutex_lock(&sLock);
		sTiming = timing;
		sFrame = frame;
		sScreen = ID().id;
		sAsked = system_time();
		pthread_mutex_unlock(&sLock);
	}

	if (timing.period <= 0 || (timing.flags & (B_SCAN_TIMING_FROM_HARDWARE
			| B_SCAN_TIMING_FROM_BLANK)) == 0) {
		return WaitForRetrace(timeout);
	}

	bigtime_t now = system_time();
	bigtime_t next = timing.scan_start;
	if (next <= now)
		next += ((now - next) / timing.period + 1) * timing.period;

	if (timeout != B_INFINITE_TIMEOUT && next - now > timeout) {
		if (timeout <= 0)
			return B_WOULD_BLOCK;
		snooze(timeout);
		return B_TIMED_OUT;
	}

	while (snooze_until(next, B_SYSTEM_TIMEBASE) == B_INTERRUPTED)
		;
	return B_OK;
}


uint8
BScreen::IndexForColor(uint8 red, uint8 green, uint8 blue, uint8 alpha)
{
	if (fScreen != NULL)
		return fScreen->IndexForColor(red, green, blue, alpha);

	return 0;
}


rgb_color
BScreen::ColorForIndex(const uint8 index)
{
	if (fScreen != NULL)
		return fScreen->ColorForIndex(index);

	return rgb_color();
}


uint8
BScreen::InvertIndex(uint8 index)
{
	if (fScreen != NULL)
		return fScreen->InvertIndex(index);

	return 0;
}


const color_map*
BScreen::ColorMap()
{
	if (fScreen != NULL)
		return fScreen->ColorMap();

	return NULL;
}


status_t
BScreen::GetBitmap(BBitmap** _bitmap, bool drawCursor, BRect* bounds)
{
	if (fScreen != NULL)
		return fScreen->GetBitmap(_bitmap, drawCursor, bounds);

	return B_ERROR;
}


status_t
BScreen::ReadBitmap(BBitmap* bitmap, bool drawCursor, BRect* bounds)
{
	if (fScreen != NULL)
		return fScreen->ReadBitmap(bitmap, drawCursor, bounds);

	return B_ERROR;
}


rgb_color
BScreen::DesktopColor()
{
	if (fScreen != NULL)
		return fScreen->DesktopColor(B_CURRENT_WORKSPACE_INDEX);

	return rgb_color();
}


rgb_color
BScreen::DesktopColor(uint32 workspace)
{
	if (fScreen != NULL)
		return fScreen->DesktopColor(workspace);

	return rgb_color();
}


void
BScreen::SetDesktopColor(rgb_color color, bool stick)
{
	if (fScreen != NULL)
		fScreen->SetDesktopColor(color, B_CURRENT_WORKSPACE_INDEX, stick);
}


void
BScreen::SetDesktopColor(rgb_color color, uint32 workspace, bool stick)
{
	if (fScreen != NULL)
		fScreen->SetDesktopColor(color, workspace, stick);
}


status_t
BScreen::ProposeMode(display_mode* target, const display_mode* low,
	const display_mode* high)
{
	if (fScreen != NULL)
		return fScreen->ProposeMode(target, low, high);

	return B_ERROR;
}


status_t
BScreen::GetModeList(display_mode** _modeList, uint32* _count)
{
	if (fScreen != NULL)
		return fScreen->GetModeList(_modeList, _count);

	return B_ERROR;
}


status_t
BScreen::GetMode(display_mode* mode)
{
	if (fScreen != NULL)
		return fScreen->GetMode(B_CURRENT_WORKSPACE_INDEX, mode);

	return B_ERROR;
}


status_t
BScreen::GetMode(uint32 workspace, display_mode* mode)
{
	if (fScreen != NULL)
		return fScreen->GetMode(workspace, mode);

	return B_ERROR;
}


status_t
BScreen::SetMode(display_mode* mode, bool makeDefault)
{
	if (fScreen != NULL)
		return fScreen->SetMode(B_CURRENT_WORKSPACE_INDEX, mode, makeDefault);

	return B_ERROR;
}


status_t
BScreen::SetMode(uint32 workspace, display_mode* mode, bool makeDefault)
{
	if (fScreen != NULL)
		return fScreen->SetMode(workspace, mode, makeDefault);

	return B_ERROR;
}


status_t
BScreen::GetDeviceInfo(accelerant_device_info* info)
{
	if (fScreen != NULL)
		return fScreen->GetDeviceInfo(info);

	return B_ERROR;
}


status_t
BScreen::GetMonitorInfo(monitor_info* info)
{
	if (fScreen != NULL)
		return fScreen->GetMonitorInfo(info);

	return B_ERROR;
}


status_t
BScreen::GetPixelClockLimits(display_mode* mode, uint32* _low, uint32* _high)
{
	if (fScreen != NULL)
		return fScreen->GetPixelClockLimits(mode, _low, _high);

	return B_ERROR;
}


status_t
BScreen::GetTimingConstraints(display_timing_constraints* constraints)
{
	if (fScreen != NULL)
		return fScreen->GetTimingConstraints(constraints);

	return B_ERROR;
}


status_t
BScreen::SetDPMS(uint32 dpmsState)
{
	if (fScreen != NULL)
		return fScreen->SetDPMS(dpmsState);

	return B_ERROR;
}


uint32
BScreen::DPMSState()
{
	if (fScreen != NULL)
		return fScreen->DPMSState();

	return 0;
}


uint32
BScreen::DPMSCapabilites()
{
	if (fScreen != NULL)
		return fScreen->DPMSCapabilites();

	return 0;
}


status_t
BScreen::GetBrightness(float* brightness)
{
	if (fScreen != NULL)
		return fScreen->GetBrightness(brightness);
	return B_ERROR;
}


/*!	Where the display output that shows \a frame (in screen coordinates;
	the output with the most of it) is in its refresh: its frames begin at
	scan_start + n * period. A program that writes into the frame buffer
	itself can time its copies by it to keep them off the scan, on each
	output at its own blanks. When generation changes, the outputs changed:
	ask again for a window that moved or a layout or mode that changed.
	Without a blank to go by, flags are 0 and scan_start 0; period is then
	the display mode's.
*/
status_t
BScreen::GetScanTiming(BRect frame, display_scan_timing* timing)
{
	if (timing == NULL)
		return B_BAD_VALUE;
	if (fScreen != NULL)
		return fScreen->GetScanTiming(frame, timing);
	return B_ERROR;
}


status_t
BScreen::SetBrightness(float brightness)
{
	if (fScreen != NULL)
		return fScreen->SetBrightness(brightness);
	return B_ERROR;
}


status_t
BScreen::GetTemperature(float* kelvin)
{
	if (fScreen != NULL)
		return fScreen->GetTemperature(kelvin);
	return B_ERROR;
}


status_t
BScreen::SetTemperature(float kelvin)
{
	if (fScreen != NULL)
		return fScreen->SetTemperature(kelvin);
	return B_ERROR;
}


//	#pragma mark - Deprecated methods


BPrivate::BPrivateScreen*
BScreen::private_screen()
{
	return fScreen;
}


status_t
BScreen::ProposeDisplayMode(display_mode* target, const display_mode* low,
	const display_mode* high)
{
	return ProposeMode(target, low, high);
}


void*
BScreen::BaseAddress()
{
	// deprecated
	return NULL;
}


uint32
BScreen::BytesPerRow()
{
	// deprecated
	return 0;
}
