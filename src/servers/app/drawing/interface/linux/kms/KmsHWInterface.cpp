/*
 * Copyright 2026, Koan contributors. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * The structure (mode, layout, render scale, cursor and retrace handling)
 * follows air/OS's AccelerantHWInterface (Copyright 2005-2016, Haiku; air/OS
 * 2026; MIT License), with KmsDevice in the accelerant's place.
 */


#include "KmsHWInterface.h"

#include <new>

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>

#include <Autolock.h>
#include <String.h>

#include "KmsEdid.h"
#include "MallocBuffer.h"
#include "ServerCursor.h"

#ifdef VOS_HAVE_KMS_UDEV
#	include <libudev.h>
extern "C" {
#	include <libbacklight.h>
}
#endif


using std::nothrow;


#define TRACE(x...) fprintf(stderr, "kms: " x)


static const int32 kDisplayChangeCode = 'dchg';


static bool
same_mode(const display_mode& a, const display_mode& b)
{
	return a.virtual_width == b.virtual_width
		&& a.virtual_height == b.virtual_height && a.space == b.space
		&& memcmp(&a.timing, &b.timing, sizeof(display_timing)) == 0;
}


/*!	app_server's way in (ScreenManager::_CreateLinuxHWInterface()): NULL when
	the backend cannot be built for this machine; otherwise an interface
	whose InitCheck() says whether a KMS device could be opened. */
HWInterface*
create_kms_hw_interface()
{
	const char* path = getenv("VOS_KMS_DEVICE");
	KmsHWInterface* interface = new(nothrow) KmsHWInterface(path);
	if (interface != NULL && interface->InitCheck() != B_OK) {
		delete interface;
		return NULL;
	}
	return interface;
}


KmsHWInterface::KmsHWInterface(const char* devicePath)
	:
	HWInterface(),
	fDevice(NULL),
	fInitStatus(B_NO_INIT),
	fFrontLocked(false),
	fSemaphoreLock("kms retrace semaphores"),
	fSemaphoreCount(0),
	fRetraceSemaphore(-1),
	fPrimaryOutput(0),
	fCursorLock("kms cursor"),
	fDisplayChangePort(-1),
	fDisplayChangeThread(-1),
	fBacklight(NULL)
{
	fDevicePath[0] = '\0';
	if (devicePath != NULL)
		strlcpy(fDevicePath, devicePath, sizeof(fDevicePath));
	memset(&fDisplayMode, 0, sizeof(fDisplayMode));
	fDisplayMode.space = B_RGB32;

	// the device is opened here, so that ScreenManager can try the next
	// backend when there is none
	const char* gpu = getenv("VOS_KMS_GPU");
	uint32 flags = gpu != NULL && strcmp(gpu, "0") == 0
		? KmsDevice::OPEN_NO_GPU : 0;
	fDevice = KmsDevice::Open(fDevicePath[0] != '\0' ? fDevicePath : NULL,
		flags, &fInitStatus);
	if (fDevice == NULL) {
		TRACE("no KMS device: %s\n", strerror(fInitStatus));
		return;
	}
	fDevice->SetListener(this);
}


KmsHWInterface::~KmsHWInterface()
{
	if (fDevice != NULL) {
		fDevice->SetFrontSource(NULL);
		fDevice->SetListener(NULL);
		delete fDevice;
	}
	for (int32 i = 0; i < fSemaphoreCount; i++)
		delete_sem(fSemaphores[i].sem);
#ifdef VOS_HAVE_KMS_UDEV
	if (fBacklight != NULL)
		backlight_destroy(fBacklight);
#endif
}


status_t
KmsHWInterface::InitCheck() const
{
	return fDevice != NULL ? B_OK : fInitStatus;
}


status_t
KmsHWInterface::Initialize()
{
	status_t status = HWInterface::Initialize();
	if (status < B_OK)
		return status;
	if (fDevice == NULL)
		return fInitStatus;

	fDevice->SetFrontSource(this);

#ifdef VOS_HAVE_KMS_UDEV
	// the built-in panel's backlight, as VitruvianOS's DRM backend finds it
	struct stat st;
	struct udev* udev = udev_new();
	if (udev != NULL && fstat(fDevice->Fd(), &st) == 0) {
		struct udev_device* device = udev_device_new_from_devnum(udev, 'c',
			st.st_rdev);
		if (device != NULL) {
			display_output* outputs = NULL;
			uint32 count = 0;
			if (fDevice->GetDisplayOutputs(&outputs, &count) == B_OK) {
				for (uint32 i = 0; i < count && fBacklight == NULL; i++) {
					if ((outputs[i].flags & B_DISPLAY_OUTPUT_INTERNAL) == 0)
						continue;
					fBacklight = backlight_init(device,
						strncmp(outputs[i].name, "eDP", 3) == 0
							? DRM_MODE_CONNECTOR_eDP
							: DRM_MODE_CONNECTOR_LVDS);
				}
				free(outputs);
			}
			udev_device_unref(device);
		}
	}
	if (udev != NULL)
		udev_unref(udev);
#endif

	// monitors that come and go: told after a burst settled, as
	// AccelerantHWInterface does with its accelerant's port
	fDisplayChangePort = create_port(8, "kms display change");
	if (fDisplayChangePort >= 0) {
		fDisplayChangeThread = spawn_thread(_DisplayChangeThread,
			"kms display change", B_NORMAL_PRIORITY, this);
		if (fDisplayChangeThread >= 0)
			resume_thread(fDisplayChangeThread);
	}

	// copies to the screen are made at each output's vertical blank
	_StartFlushing();
	return B_OK;
}


status_t
KmsHWInterface::Shutdown()
{
	_StopFlushing();
	_ReleaseFlushShadows();
	if (fDisplayChangeThread >= 0) {
		delete_port(fDisplayChangePort);
		fDisplayChangePort = -1;
		status_t result;
		wait_for_thread(fDisplayChangeThread, &result);
		fDisplayChangeThread = -1;
	}
	if (fDevice != NULL)
		fDevice->SetFrontSource(NULL);
	return B_OK;
}


/*static*/ status_t
KmsHWInterface::_DisplayChangeThread(void* data)
{
	KmsHWInterface* interface = (KmsHWInterface*)data;
	while (true) {
		int32 code;
		ssize_t bytes = read_port(interface->fDisplayChangePort, &code, NULL,
			0);
		if (bytes == B_BAD_PORT_ID || bytes == B_INTERRUPTED)
			break;
		if (bytes < B_OK || code != kDisplayChangeCode)
			continue;
		// a burst of events (one per connector) is one change
		snooze(200000);
		while (port_count(interface->fDisplayChangePort) > 0) {
			read_port_etc(interface->fDisplayChangePort, &code, NULL, 0,
				B_RELATIVE_TIMEOUT, 0);
		}
		interface->_NotifyScreenChanged();
	}
	return B_OK;
}


// #pragma mark - KmsDeviceListener


void
KmsHWInterface::KmsRetrace(uint32 outputID, bigtime_t when)
{
	// every waiter goes, nothing is left for later ones
	sem_id semaphore = _SemaphoreFor(outputID);
	if (semaphore < 0)
		return;
	int32 count = 0;
	if (get_sem_count(semaphore, &count) == B_OK && count < 0)
		release_sem_etc(semaphore, -count, B_DO_NOT_RESCHEDULE);
	if (outputID == fPrimaryOutput && fRetraceSemaphore >= 0
		&& get_sem_count(fRetraceSemaphore, &count) == B_OK && count < 0)
		release_sem_etc(fRetraceSemaphore, -count, B_DO_NOT_RESCHEDULE);
}


void
KmsHWInterface::KmsOutputsChanged()
{
	if (fDisplayChangePort >= 0) {
		write_port_etc(fDisplayChangePort, kDisplayChangeCode, NULL, 0,
			B_RELATIVE_TIMEOUT, 0);
	}
}


void
KmsHWInterface::KmsSessionChanged(bool active)
{
	if (!active)
		return;
	// the scan-out buffers were shown again from what the frame buffer
	// holds; the hardware cursor is back too
	_UpdateHardwareCursor(true);
}


void
KmsHWInterface::OnSeatEnabled()
{
	if (fDevice != NULL)
		fDevice->SetSessionActive(true);
}


void
KmsHWInterface::OnSeatDisabled()
{
	if (fDevice != NULL)
		fDevice->SetSessionActive(false);
}


// #pragma mark - KmsFrontSource


bool
KmsHWInterface::LockFront(KmsFrontView& view)
{
	if (!LockParallelAccess())
		return false;
	RenderingBuffer* front = fFrontBuffer.Get();
	if (front == NULL || front->Bits() == NULL) {
		UnlockParallelAccess();
		return false;
	}
	view.bits = (const uint8*)front->Bits();
	view.bytesPerRow = front->BytesPerRow();
	view.width = front->Width();
	view.height = front->Height();
	return true;
}


void
KmsHWInterface::UnlockFront()
{
	UnlockParallelAccess();
}


// #pragma mark - modes


status_t
KmsHWInterface::SetMode(const display_mode& mode)
{
	ForgetStale();
	status_t status = _SetMode(mode, false);
	// a mode set takes the cursor planes away
	_UpdateHardwareCursor(true);
	return status;
}


status_t
KmsHWInterface::_SetMode(const display_mode& mode, bool force)
{
	AutoWriteLocker _(this);
	if (fDevice == NULL)
		return B_NO_INIT;
	if (!force && fFrontBuffer.IsSet() && same_mode(fDisplayMode, mode))
		return B_OK;
	if (!_IsValidMode(mode))
		return B_BAD_VALUE;

	display_mode newMode = mode;
	status_t status = fDevice->SetMode(&newMode);
	if (status != B_OK) {
		if (fFrontBuffer.IsSet())
			return status;
		// the first mode set: the monitors' own
		TRACE("mode %ux%u refused (%s), the preferred one instead\n",
			mode.virtual_width, mode.virtual_height, strerror(status));
		if (fDevice->GetPreferredMode(&newMode) != B_OK)
			return status;
		status = fDevice->SetMode(&newMode);
		if (status != B_OK)
			return status;
		SetLogicalSize(0, 0);
	}
	fDisplayMode = newMode;

	status = _UpdateBuffers();
	if (status != B_OK)
		return status;

	// the outputs show other parts of it now
	_UpdateFlushTargets();
	{
		BAutolock _(fSemaphoreLock);
		display_output* outputs = NULL;
		uint32 count = 0;
		fPrimaryOutput = 0;
		if (fDevice->GetDisplayOutputs(&outputs, &count) == B_OK) {
			int32 best = -1;
			for (uint32 i = 0; i < count; i++) {
				const display_output& output = outputs[i];
				if ((output.flags & B_DISPLAY_OUTPUT_ENABLED) == 0
					|| (output.flags & B_DISPLAY_OUTPUT_MIRROR) != 0)
					continue;
				if (best < 0 || output.y < outputs[best].y
					|| (output.y == outputs[best].y
						&& output.x < outputs[best].x))
					best = i;
			}
			if (best >= 0)
				fPrimaryOutput = outputs[best].id;
			free(outputs);
		}
	}
	_NotifyFrameBufferChanged();
	fDevice->DamageAll();
	return B_OK;
}


/*!	The front buffer is the frame buffer's size (the layout's); the back
	buffer as AccelerantHWInterface keeps it (the front's size, or the
	logical size times the render scale with a software scale). */
status_t
KmsHWInterface::_UpdateBuffers()
{
	uint32 width = fDisplayMode.virtual_width;
	uint32 height = fDisplayMode.virtual_height;
	if (!fFrontBuffer.IsSet() || fFrontBuffer->Width() != width
		|| fFrontBuffer->Height() != height) {
		MallocBuffer* front = new(nothrow) MallocBuffer(width, height);
		if (front == NULL || front->InitCheck() != B_OK) {
			delete front;
			return B_NO_MEMORY;
		}
		uint32* pixels = (uint32*)front->Bits();
		for (size_t i = 0; i < (size_t)width * height; i++)
			pixels[i] = 0xff000000;
		fFrontBuffer.SetTo(front);
	}

	int32 backWidth = BackBufferWidth();
	int32 backHeight = BackBufferHeight();
	if (fBackBuffer.IsSet() && fBackBuffer->Width() == (uint32)backWidth
		&& fBackBuffer->Height() == (uint32)backHeight)
		return B_OK;
	fBackBuffer.Unset();
	MallocBuffer* back = new(nothrow) MallocBuffer(backWidth, backHeight);
	if (back == NULL || back->InitCheck() != B_OK) {
		delete back;
		return B_NO_MEMORY;
	}
	// alpha is 255 this way
	memset(back->Bits(), 255, back->BitsLength());
	fBackBuffer.SetTo(back);
	return B_OK;
}


void
KmsHWInterface::GetMode(display_mode* mode)
{
	if (mode != NULL && LockParallelAccess()) {
		*mode = fDisplayMode;
		UnlockParallelAccess();
	}
}


status_t
KmsHWInterface::GetDeviceInfo(accelerant_device_info* info)
{
	if (fDevice == NULL)
		return B_NO_INIT;
	memset(info, 0, sizeof(accelerant_device_info));
	info->version = B_ACCELERANT_VERSION;
	snprintf(info->name, sizeof(info->name), "KMS %s", fDevice->DriverName());
	snprintf(info->chipset, sizeof(info->chipset), "%s",
		fDevice->UsesGpu() ? fDevice->GpuRenderer() : fDevice->DriverName());
	strlcpy(info->serial_no, fDevice->Path(), sizeof(info->serial_no));
	return B_OK;
}


status_t
KmsHWInterface::GetModeList(display_mode** _modes, uint32* _count)
{
	AutoReadLocker _(this);
	if (fDevice == NULL)
		return B_NO_INIT;
	display_mode* modes;
	uint32 count;
	status_t status = fDevice->GetModeList(&modes, &count);
	if (status != B_OK)
		return status;
	// the caller delete[]s it
	*_modes = new(nothrow) display_mode[count];
	if (*_modes == NULL) {
		free(modes);
		return B_NO_MEMORY;
	}
	memcpy(*_modes, modes, count * sizeof(display_mode));
	free(modes);
	*_count = count;
	return B_OK;
}


status_t
KmsHWInterface::GetPixelClockLimits(display_mode* mode, uint32* _low,
	uint32* _high)
{
	// layout modes are wide
	*_low = 25000;
	*_high = 2400000;
	return B_OK;
}


status_t
KmsHWInterface::GetTimingConstraints(display_timing_constraints* constraints)
{
	return B_UNSUPPORTED;
}


status_t
KmsHWInterface::ProposeMode(display_mode* candidate, const display_mode* low,
	const display_mode* high)
{
	if (candidate == NULL || fDevice == NULL)
		return B_BAD_VALUE;
	if (candidate->virtual_width < 320 || candidate->virtual_height < 200)
		return B_BAD_VALUE;
	if ((fDevice->MaxWidth() != 0
			&& candidate->virtual_width > fDevice->MaxWidth())
		|| (fDevice->MaxHeight() != 0
			&& candidate->virtual_height > fDevice->MaxHeight()))
		return B_BAD_VALUE;
	return B_OK;
}


status_t
KmsHWInterface::GetPreferredMode(display_mode* mode)
{
	if (fDevice == NULL)
		return B_NO_INIT;
	return fDevice->GetPreferredMode(mode);
}


status_t
KmsHWInterface::GetMonitorInfo(monitor_info* info)
{
	if (fDevice == NULL)
		return B_NO_INIT;
	uint8 edid[B_DISPLAY_OUTPUT_EDID_LENGTH];
	uint32 length = sizeof(edid);
	status_t status = fDevice->GetEdid(0, edid, &length);
	if (status != B_OK)
		return B_NOT_SUPPORTED;
	return kms_edid_monitor_info(edid, length, info);
}


// #pragma mark - display layout and scale


status_t
KmsHWInterface::GetDisplayOutputs(display_output** _outputs, uint32* _count)
{
	if (fDevice == NULL)
		return B_NO_INIT;
	return fDevice->GetDisplayOutputs(_outputs, _count);
}


status_t
KmsHWInterface::GetDisplayOutputModes(uint32 id, display_mode** _modes,
	uint32* _count)
{
	if (fDevice == NULL)
		return B_NO_INIT;
	return fDevice->GetDisplayOutputModes(id, _modes, _count);
}


status_t
KmsHWInterface::SetDisplayLayout(const display_output_config* configs,
	uint32 count, bool switchMode)
{
	if (fDevice == NULL)
		return B_NO_INIT;

	display_mode mode;
	status_t status;
	int32 oldWidth = fLogicalWidth;
	int32 oldHeight = fLogicalHeight;
	uint16 oldRenderScale = RenderScale();
	{
		AutoWriteLocker _(this);
		status = fDevice->SetDisplayLayout(configs, count, &mode);
		if (status == B_OK) {
			// as AccelerantHWInterface: one density for all, the logical
			// size the rectangle around the monitors that are not mirrors
			uint16 renderScale = 100;
			int32 right = 0, bottom = 0;
			for (uint32 i = 0; i < count; i++) {
				if (configs[i].render_scale > renderScale)
					renderScale = configs[i].render_scale;
				if ((configs[i].flags & B_DISPLAY_OUTPUT_ENABLED) == 0
					|| (configs[i].flags & B_DISPLAY_OUTPUT_MIRROR) != 0)
					continue;
				uint16 scale = configs[i].scale == 0 ? 100 : configs[i].scale;
				int32 width = (configs[i].timing.h_display * 100 + scale / 2)
					/ scale;
				int32 height = (configs[i].timing.v_display * 100 + scale / 2)
					/ scale;
				if (configs[i].x + width > right)
					right = configs[i].x + width;
				if (configs[i].y + height > bottom)
					bottom = configs[i].y + height;
			}
			SetLogicalSize(right, bottom);
			HWInterface::SetRenderScale(renderScale);
		}
	}
	if (status != B_OK || !switchMode)
		return status;

	status = _SetMode(mode, true);
	if (status != B_OK) {
		AutoWriteLocker _(this);
		SetLogicalSize(oldWidth, oldHeight);
		HWInterface::SetRenderScale(oldRenderScale);
	}
	_UpdateHardwareCursor(true);
	return status;
}


status_t
KmsHWInterface::SetRenderScale(uint16 percent)
{
	AutoWriteLocker _(this);
	if (percent == RenderScale())
		return B_OK;
	status_t status = HWInterface::SetRenderScale(percent);
	if (status != B_OK || !fFrontBuffer.IsSet())
		return status;
	status = _UpdateBuffers();
	if (status != B_OK)
		return status;
	_NotifyFrameBufferChanged();
	if (fDevice != NULL)
		fDevice->DamageAll();
	_UpdateHardwareCursor(true);
	return B_OK;
}


status_t
KmsHWInterface::SetSoftwareScale(uint16 percent)
{
	AutoWriteLocker _(this);
	if (percent == SoftwareScale())
		return B_OK;
	if (!fFrontBuffer.IsSet())
		return B_NO_INIT;
	status_t status = HWInterface::SetSoftwareScale(percent);
	if (status != B_OK)
		return status;
	status = _UpdateBuffers();
	if (status != B_OK)
		return status;
	_NotifyFrameBufferChanged();
	_UpdateHardwareCursor(true);
	return B_OK;
}


// #pragma mark - retrace


sem_id
KmsHWInterface::_SemaphoreFor(uint32 id)
{
	BAutolock _(fSemaphoreLock);
	for (int32 i = 0; i < fSemaphoreCount; i++) {
		if (fSemaphores[i].id == id)
			return fSemaphores[i].sem;
	}
	if (fSemaphoreCount >= (int32)kKmsMaxOutputs)
		return B_NO_MORE_SEMS;
	char name[B_OS_NAME_LENGTH];
	snprintf(name, sizeof(name), "kms retrace %" B_PRIu32, id);
	sem_id sem = create_sem(0, name);
	if (sem < 0)
		return sem;
	fSemaphores[fSemaphoreCount].id = id;
	fSemaphores[fSemaphoreCount].sem = sem;
	fSemaphoreCount++;
	return sem;
}


/*!	The primary output's blanks. Asked for with the interface read locked:
	it locks nothing but its own. */
sem_id
KmsHWInterface::RetraceSemaphore()
{
	if (fDevice == NULL)
		return B_UNSUPPORTED;
	BAutolock _(fSemaphoreLock);
	if (fRetraceSemaphore < 0)
		fRetraceSemaphore = create_sem(0, "kms retrace");
	return fRetraceSemaphore;
}


sem_id
KmsHWInterface::OutputRetraceSemaphore(uint32 id)
{
	if (fDevice == NULL || id == 0)
		return B_UNSUPPORTED;
	fDevice->WantRetrace();
	return _SemaphoreFor(id);
}


status_t
KmsHWInterface::OutputScanLine(uint32 id, int32* _line, int32* _lines,
	bigtime_t* _scanStart, bigtime_t* _period)
{
	if (fDevice == NULL || getenv("VOS_KMS_NO_SCAN_LINE") != NULL)
		return B_UNSUPPORTED;
	bigtime_t scanStart;
	status_t status = fDevice->ScanLine(id, _line, _lines, &scanStart,
		_period);
	if (status != B_OK)
		return status;
	// DRM's clock is CLOCK_MONOTONIC, system_time() CLOCK_BOOTTIME
	*_scanStart = scanStart + (system_time() - kms_monotonic_time());
	return B_OK;
}


status_t
KmsHWInterface::WaitForRetrace(bigtime_t timeout)
{
	sem_id sem = RetraceSemaphore();
	if (sem < 0)
		return sem;
	fDevice->WantRetrace();
	return acquire_sem_etc(sem, 1, B_RELATIVE_TIMEOUT, timeout);
}


// #pragma mark - power


status_t
KmsHWInterface::SetDPMSMode(uint32 state)
{
	if (fDevice == NULL)
		return B_NO_INIT;
	status_t status = fDevice->SetDPMSMode(state);
	if (status == B_OK && state == B_DPMS_ON)
		_UpdateHardwareCursor(true);
	return status;
}


uint32
KmsHWInterface::DPMSMode()
{
	return fDevice != NULL ? fDevice->DPMSMode() : B_DPMS_ON;
}


uint32
KmsHWInterface::DPMSCapabilities()
{
	return B_DPMS_ON | B_DPMS_STAND_BY | B_DPMS_SUSPEND | B_DPMS_OFF;
}


status_t
KmsHWInterface::SetBrightness(float brightness)
{
#ifdef VOS_HAVE_KMS_UDEV
	if (fBacklight == NULL)
		return B_NOT_SUPPORTED;
	long maximum = backlight_get_max_brightness(fBacklight);
	if (maximum <= 0)
		return B_ERROR;
	// all the way down it still glows a little
	long level = (long)(brightness * maximum + 0.5f);
	if (level < maximum / 100)
		level = maximum / 100;
	if (level < 1)
		level = 1;
	if (level > maximum)
		level = maximum;
	return backlight_set_brightness(fBacklight, level) < 0 ? B_ERROR : B_OK;
#else
	return B_NOT_SUPPORTED;
#endif
}


status_t
KmsHWInterface::GetBrightness(float* brightness)
{
#ifdef VOS_HAVE_KMS_UDEV
	if (fBacklight == NULL)
		return B_NOT_SUPPORTED;
	long maximum = backlight_get_max_brightness(fBacklight);
	long level = backlight_get_brightness(fBacklight);
	if (maximum <= 0 || level < 0)
		return B_ERROR;
	*brightness = (float)level / maximum;
	return B_OK;
#else
	return B_NOT_SUPPORTED;
#endif
}


status_t
KmsHWInterface::SetTemperature(float kelvin)
{
	if (fDevice == NULL)
		return B_NO_INIT;
	return fDevice->SetTemperature(kelvin);
}


status_t
KmsHWInterface::GetTemperature(float* kelvin)
{
	if (fDevice == NULL)
		return B_NO_INIT;
	*kelvin = fDevice->Temperature();
	return B_OK;
}


status_t
KmsHWInterface::GetConnectorName(BString& name)
{
	if (fDevice == NULL)
		return B_NO_INIT;
	char buffer[B_DISPLAY_OUTPUT_NAME_LENGTH];
	status_t status = fDevice->GetOutputName(0, buffer, sizeof(buffer));
	if (status == B_OK)
		name = buffer;
	return status;
}


status_t
KmsHWInterface::GetAccelerantPath(BString& path)
{
	return B_NOT_SUPPORTED;
}


status_t
KmsHWInterface::GetDriverPath(BString& path)
{
	if (fDevice == NULL)
		return B_NO_INIT;
	path = fDevice->Path();
	return B_OK;
}


// #pragma mark - cursor


void
KmsHWInterface::SetCursor(ServerCursor* cursor)
{
	if (cursor == NULL)
		return;
	// HWInterface makes the cursor at the frame buffer's density first
	HWInterface::SetCursor(cursor);
	_UpdateHardwareCursor();
}


void
KmsHWInterface::SetDragBitmap(const ServerBitmap* bitmap,
	const BPoint& offsetFromCursor)
{
	HWInterface::SetDragBitmap(bitmap, offsetFromCursor);
	_UpdateHardwareCursor();
}


/*!	Hands the cursor (at the frame buffer's density, the drag bitmap
	composed into it) to the cursor planes, and switches between them and
	the software cursor as the device takes it or not; as
	AccelerantHWInterface::_UpdateHardwareCursor(). */
void
KmsHWInterface::_UpdateHardwareCursor(bool force)
{
	if (fDevice == NULL)
		return;
	ServerCursorReference cursor = CursorAndDragBitmap();
	if (cursor.Get() == NULL || !LockExclusiveAccess())
		return;
	BAutolock cursorLock(fCursorLock);

	if (!force && fHardwareCursorEnabled
		&& fHardwareCursor.Get() == cursor.Get()) {
		UnlockExclusiveAccess();
		return;
	}

	bool cursorSet = false;
	const char* software = getenv("VOS_KMS_SOFTWARE_CURSOR");
	if (SoftwareScale() == 100 && software == NULL
		&& (cursor->ColorSpace() == B_RGBA32
			|| cursor->ColorSpace() == B_RGB32)) {
		// the pointer's buffer pixel first, so that the planes come up
		// where it is
		float factor = RenderScaleFactor();
		fDevice->MoveCursor((int32)floorf(fCursorLocation.x * factor),
			(int32)floorf(fCursorLocation.y * factor));
		fDevice->ShowCursor(fCursorVisible);
		cursorSet = fDevice->SetCursor((const uint8*)cursor->Bits(),
			cursor->BytesPerRow(), cursor->Width(), cursor->Height(),
			(int32)cursor->GetHotSpot().x, (int32)cursor->GetHotSpot().y)
				== B_OK;
	}

	if (cursorSet && !fHardwareCursorEnabled) {
		// from the software cursor: erase it
		if (fCursorVisible && fFloatingOverlaysLock.Lock()) {
			IntRect r = _CursorFrame();
			fCursorVisible = false;
			_RestoreCursorArea();
			Invalidate(r);
			fCursorVisible = true;
			fFloatingOverlaysLock.Unlock();
		}
	}
	if (!cursorSet)
		fDevice->ShowCursor(false);

	bool wasEnabled = fHardwareCursorEnabled;
	fHardwareCursorEnabled = cursorSet;
	fHardwareCursor.SetTo(cursorSet ? cursor.Get() : NULL);

	if (wasEnabled && !cursorSet) {
		if (fFloatingOverlaysLock.Lock()) {
			Invalidate(_CursorFrame());
			fFloatingOverlaysLock.Unlock();
		}
	}
	UnlockExclusiveAccess();
}


void
KmsHWInterface::SetCursorVisible(bool visible)
{
	HWInterface::SetCursorVisible(visible);
	// the planes' own lock only: the exclusive lock would hold every mouse
	// move up behind the drawing under way
	if (fHardwareCursorEnabled && fCursorLock.Lock()) {
		fDevice->ShowCursor(visible);
		fCursorLock.Unlock();
	}
}


void
KmsHWInterface::MoveCursorTo(float x, float y)
{
	HWInterface::MoveCursorTo(x, y);
	if (fHardwareCursorEnabled && fCursorLock.Lock()) {
		// the planes are placed in buffer pixels, the location is logical
		float factor = RenderScaleFactor();
		fDevice->MoveCursor((int32)floorf(x * factor),
			(int32)floorf(y * factor));
		fCursorLock.Unlock();
	}
}


// #pragma mark - buffers


RenderingBuffer*
KmsHWInterface::FrontBuffer() const
{
	return fFrontBuffer.Get();
}


RenderingBuffer*
KmsHWInterface::BackBuffer() const
{
	return fBackBuffer.Get();
}


bool
KmsHWInterface::IsDoubleBuffered() const
{
	return fBackBuffer.IsSet();
}


/*!	Drawing that goes to the screen at the next blank: the blanks are
	followed while it waits. */
status_t
KmsHWInterface::CopyBackToFront(const BRect& frame)
{
	if (fDevice != NULL)
		fDevice->WantRetrace();
	return HWInterface::CopyBackToFront(frame);
}


/*!	Every copy into the front buffer ends here (the flush, the copies made
	as drawn, the software cursor): what changed goes to the monitors. */
void
KmsHWInterface::_DrawCursor(IntRect area) const
{
	HWInterface::_DrawCursor(area);
	if (fDevice == NULL || !area.IsValid())
		return;

	IntRect changed = area;
	IntRect cursor = _CursorFrame();
	if (cursor.IsValid() && cursor.Intersects(area))
		changed = changed | cursor;

	// in the front buffer's pixels (a software scale enlarges the back)
	RenderingBuffer* front = fFrontBuffer.Get();
	RenderingBuffer* back = fBackBuffer.Get();
	if (front != NULL && back != NULL && back->Width() != front->Width()) {
		float sx = (float)front->Width() / back->Width();
		float sy = (float)front->Height() / back->Height();
		changed = IntRect((int32)floorf(changed.left * sx) - 1,
			(int32)floorf(changed.top * sy) - 1,
			(int32)ceilf((changed.right + 1) * sx),
			(int32)ceilf((changed.bottom + 1) * sy));
	}
	fDevice->Damage(KmsRect(changed.left, changed.top,
		changed.right - changed.left + 1, changed.bottom - changed.top + 1));
}
