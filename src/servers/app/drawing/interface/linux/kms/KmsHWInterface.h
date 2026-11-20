/*
 * Copyright 2026, Koan contributors. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef KMS_HW_INTERFACE_H
#define KMS_HW_INTERFACE_H


/*	air/OS's HWInterface on a Linux KMS device: what AccelerantHWInterface
	is on a multi-head accelerant, with KmsDevice in the accelerant's place.
	Every connector is a display_output with its own blanks (a retrace
	semaphore each, the vertical blank flush per output), its region of
	the frame buffer at the render scale, its EDID; the cursor is on each
	CRTC's cursor plane at the monitor's density.

	The front buffer is app_server's own memory (an area, so that direct
	windows can map it): the flush copies into it at each output's blank as
	into an accelerant's frame buffer, and KmsDevice takes what changed
	from there into its page flipped scan-out buffers (GPU or CPU).
*/


#include "HWInterface.h"

#include <AutoDeleter.h>
#include <Locker.h>

#include "KmsDevice.h"


class MallocBuffer;


class KmsHWInterface : public HWInterface, private KmsFrontSource,
	private KmsDeviceListener {
public:
								KmsHWInterface(const char* devicePath = NULL);
	virtual						~KmsHWInterface();

	virtual	status_t			Initialize();
	virtual	status_t			Shutdown();
	virtual	status_t			InitCheck() const;

	virtual	status_t			SetMode(const display_mode& mode);
	virtual	void				GetMode(display_mode* mode);

	virtual status_t			GetDeviceInfo(accelerant_device_info* info);
	virtual status_t			GetModeList(display_mode** _modeList,
									uint32* _count);
	virtual status_t			GetPixelClockLimits(display_mode* mode,
									uint32* _low, uint32* _high);
	virtual status_t			GetTimingConstraints(display_timing_constraints*
									constraints);
	virtual status_t			ProposeMode(display_mode* candidate,
									const display_mode* low,
									const display_mode* high);
	virtual	status_t			GetPreferredMode(display_mode* mode);
	virtual status_t			GetMonitorInfo(monitor_info* info);

	virtual	bool				HasDisplayLayout() const { return true; }
	virtual	status_t			SetSoftwareScale(uint16 percent);
	virtual	status_t			SetRenderScale(uint16 percent);
	virtual	status_t			GetDisplayOutputs(display_output** _outputs,
									uint32* _count);
	virtual	status_t			GetDisplayOutputModes(uint32 id,
									display_mode** _modes, uint32* _count);
	virtual	status_t			SetDisplayLayout(
									const display_output_config* configs,
									uint32 count, bool switchMode = true);

	virtual sem_id				RetraceSemaphore();
	virtual	sem_id				OutputRetraceSemaphore(uint32 id);
	virtual	status_t			OutputScanLine(uint32 id, int32* _line,
									int32* _lines, bigtime_t* _scanStart,
									bigtime_t* _period);
	virtual status_t			WaitForRetrace(
									bigtime_t timeout = B_INFINITE_TIMEOUT);

	virtual status_t			SetDPMSMode(uint32 state);
	virtual uint32				DPMSMode();
	virtual uint32				DPMSCapabilities();

	virtual status_t			SetBrightness(float brightness);
	virtual status_t			GetBrightness(float* brightness);
	virtual status_t			SetTemperature(float kelvin);
	virtual status_t			GetTemperature(float* kelvin);
	virtual status_t			GetConnectorName(BString& name);

	virtual status_t			GetAccelerantPath(BString& path);
	virtual status_t			GetDriverPath(BString& path);

	virtual	void				OnSeatEnabled();
	virtual	void				OnSeatDisabled();

	virtual	void				SetCursor(ServerCursor* cursor);
	virtual	void				SetCursorVisible(bool visible);
	virtual	void				MoveCursorTo(float x, float y);
	virtual	void				SetDragBitmap(const ServerBitmap* bitmap,
									const BPoint& offsetFromCursor);

	virtual	RenderingBuffer*	FrontBuffer() const;
	virtual	RenderingBuffer*	BackBuffer() const;
	virtual	bool				IsDoubleBuffered() const;

	virtual	status_t			CopyBackToFront(const BRect& frame);

protected:
	virtual	void				_DrawCursor(IntRect area) const;

private:
	// KmsFrontSource
	virtual	bool				LockFront(KmsFrontView& view);
	virtual	void				UnlockFront();

	// KmsDeviceListener
	virtual	void				KmsRetrace(uint32 outputID, bigtime_t when);
	virtual	void				KmsOutputsChanged();
	virtual	void				KmsSessionChanged(bool active);

			status_t			_SetMode(const display_mode& mode, bool force);
			status_t			_UpdateBuffers();
			void				_UpdateHardwareCursor(bool force = false);
			void				_MoveHardwareCursor(float x, float y);
			sem_id				_SemaphoreFor(uint32 id);
	static	status_t			_DisplayChangeThread(void* data);

			char				fDevicePath[64];
			KmsDevice*			fDevice;
			status_t			fInitStatus;

			ObjectDeleter<MallocBuffer>	fFrontBuffer;
			ObjectDeleter<RenderingBuffer> fBackBuffer;
			display_mode		fDisplayMode;
			bool				fFrontLocked;

			// retrace semaphores, one per output (connector), the main
			// one the primary output's
			BLocker				fSemaphoreLock;
			struct Semaphore {
				uint32			id;
				sem_id			sem;
			};
			Semaphore			fSemaphores[kKmsMaxOutputs];
			int32				fSemaphoreCount;
			sem_id				fRetraceSemaphore;
			uint32				fPrimaryOutput;

			ServerCursorReference fHardwareCursor;
			BLocker				fCursorLock;

			port_id				fDisplayChangePort;
			thread_id			fDisplayChangeThread;

			struct backlight*	fBacklight;
};


HWInterface*	create_kms_hw_interface();


#endif	// KMS_HW_INTERFACE_H
