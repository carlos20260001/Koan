/*
 * Copyright 2026, Koan contributors. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef KMS_DEVICE_H
#define KMS_DEVICE_H


/*	A KMS device (/dev/dri/cardN) as air/OS's multi-head accelerants are to
	AccelerantHWInterface: its connectors are display outputs, each showing
	a region of one frame buffer that app_server draws into (at the render
	scale), arranged by display layouts. Unlike those accelerants, the
	frame buffer is not scanned out itself: each region is copied (and
	resampled to its monitor's mode where the sizes differ) into a ring of
	scan-out buffers and page flipped, so nothing tears whatever the timing.
	The display engine scales a region where it can (atomic plane scaling);
	else the GPU (KmsGles) or the CPU (KmsScaler) resample it.

	Threads: the caller's (app_server's), an event thread (the DRM events:
	flips done and vertical blanks; udev hot plug; the seat) and a present
	thread that copies the damaged parts of the frame buffer into the
	scan-out buffers and flips them. The present thread reads the frame
	buffer between KmsFrontSource::LockFront() and UnlockFront() (app_server's
	read lock); SetMode() and SetDisplayLayout() are called with that lock
	held for writing, so they never run while a copy reads the buffer.

	Times are CLOCK_MONOTONIC microseconds, DRM's time stamps' clock.
*/


#include "KmsBlit.h"
#include "KmsLayout.h"
#include "KmsSession.h"

#include <Accelerant.h>

#include <pthread.h>
#include <stdint.h>
#include <sys/types.h>

#include <vector>


class KmsBuffer;
class KmsGles;
struct KmsGlTarget;
struct gbm_device;
struct udev;
struct udev_monitor;


struct KmsFrontView {
	const uint8*		bits;
	uint32				bytesPerRow;
	uint32				width;
	uint32				height;
};


class KmsFrontSource {
public:
	virtual						~KmsFrontSource() {}
	// the frame buffer app_server draws into (B_RGB32), read locked
	virtual	bool				LockFront(KmsFrontView& view) = 0;
	virtual	void				UnlockFront() = 0;
};


class KmsDeviceListener {
public:
	virtual						~KmsDeviceListener() {}
	// a vertical blank of that output (called on the event thread with the
	// device locked: do not call back into the device)
	virtual	void				KmsRetrace(uint32 outputID, bigtime_t when) {}
	// monitors came or went (or were exchanged): ask for the outputs again
	virtual	void				KmsOutputsChanged() {}
	// the seat came back (VT switch, resume): the screen is shown again
	virtual	void				KmsSessionChanged(bool active) {}
};


bigtime_t	kms_monotonic_time();


class KmsDevice : private KmsSession::Listener {
public:
	enum {
		OPEN_NO_GPU		= 1 << 0	// no GPU pass (as VOS_KMS_GPU=0)
	};

	static	KmsDevice*			Open(const char* path, uint32 flags,
									status_t* error = NULL);
								~KmsDevice();

			void				SetListener(KmsDeviceListener* listener);
			void				SetFrontSource(KmsFrontSource* source);

			int					Fd() const { return fSession.Fd(); }
			const char*			Path() const { return fSession.Path(); }
			const char*			DriverName() const { return fDriverName; }
			const char*			SessionKind() const
									{ return fSession.How(); }
			bool				IsAtomic() const { return fAtomic; }
			bool				UsesGpu() const;
			const char*			GpuRenderer() const;
			uint32				MaxWidth() const { return fMaxWidth; }
			uint32				MaxHeight() const { return fMaxHeight; }

	// outputs and layouts (HWInterface's display_output API)
			status_t			GetDisplayOutputs(display_output** _outputs,
									uint32* _count);
			status_t			GetDisplayOutputModes(uint32 id,
									display_mode** _modes, uint32* _count);
			status_t			SetDisplayLayout(
									const display_output_config* configs,
									uint32 count, display_mode* _mode);
			status_t			SetMode(display_mode* mode);
			status_t			GetMode(display_mode* mode);
			status_t			GetPreferredMode(display_mode* mode);
			status_t			GetModeList(display_mode** _modes,
									uint32* _count);
			status_t			GetEdid(uint32 outputID, uint8* edid,
									uint32* _length);
									// outputID 0: the primary output
			bool				IsModeSet() const { return fModeSet; }

	// presentation
			void				Damage(const KmsRect& area);
									// in frame buffer pixels
			void				DamageAll();
			void				WantRetrace();
			status_t			WaitIdle(bigtime_t timeout);
									// everything damaged is on screen
			status_t			ScanLine(uint32 outputID, int32* _line,
									int32* _lines, bigtime_t* _scanStart,
									bigtime_t* _period);
			status_t			WaitForRetrace(uint32 outputID,
									bigtime_t timeout);
									// for tests: the next blank
			int64				FlipCount() const { return fFlips; }

	// power
			status_t			SetDPMSMode(uint32 mode);
			uint32				DPMSMode() const { return fDPMSMode; }

	// the cursor plane: an image at the frame buffer's density
	// (premultiplied B_RGBA32), placed by its hot spot in buffer pixels
			status_t			SetCursor(const uint8* bits,
									uint32 bytesPerRow, uint32 width,
									uint32 height, int32 hotX, int32 hotY);
			void				MoveCursor(int32 x, int32 y);
			void				ShowCursor(bool visible);
			bool				HasCursor() const { return fCursorOk; }

	// gamma: the colour temperature of every output (6500 K: neutral)
			status_t			SetTemperature(float kelvin);
			float				Temperature() const { return fTemperature; }
			status_t			GetOutputName(uint32 outputID, char* name,
									size_t size);
									// outputID 0: the primary output

	// hot plug: probes the connectors again (udev events call this)
			bool				CheckOutputs();

	// the seat as janus_session (app_server's B_SEAT_* messages) says
			void				SetSessionActive(bool active);

	// for tests: what an output shows now, its buffer read back
			status_t			ReadBack(uint32 outputID,
									std::vector<uint32>& pixels,
									uint32* _width, uint32* _height,
									KmsRect* _destination = NULL);

private:
			struct Crtc;
			struct Output;
			struct Scanout;

								KmsDevice(uint32 flags);
			status_t			_Init(const char* path);
			status_t			_Probe();
			status_t			_ProbeOutputs(bool probe);
			void				_FillLayout(KmsLayout& layout) const;
			bool				_OutputsDiffer(const std::vector<Output>&
									probed) const;
			Output*				_FindOutput(uint32 id);
			uint32				_PropertyId(uint32 object, uint32 type,
									const char* name);

			status_t			_ApplyLayout(KmsLayout& layout);
			status_t			_AssignCrtcs(KmsLayout& layout,
									std::vector<int32>& crtcs);
			status_t			_BuildScanouts(const KmsLayout& layout,
									const std::vector<int32>& crtcs,
									bool allowPlaneScaling,
									std::vector<Scanout*>& scanouts);
			status_t			_CommitModeset(const KmsLayout& layout,
									const std::vector<int32>& crtcs,
									std::vector<Scanout*>& scanouts,
									bool testOnly);
			void				_DestroyScanouts(std::vector<Scanout*>&
									scanouts);
			void				_DestroyScanout(Scanout* scanout);
			KmsBuffer*			_CreateScanoutBuffer(uint32 width,
									uint32 height);
			status_t			_Drain(bigtime_t timeout);

			void				_DamageLocked(const KmsRect& area);
			bool				_CanRender(Scanout* scanout) const;
			int32				_FreeSlot(Scanout* scanout) const;
			void				_Render(Scanout* scanout,
									const KmsFrontView& front,
									std::vector<KmsRect>& damage, int32 slot,
									bool full);
			status_t			_Commit(Scanout* scanout);
			void				_FlipDone(Scanout* scanout, uint32 crtcId,
									bigtime_t when);
			void				_ArmBlanks();
			int					_TimedBlanks();

			void				_ApplyCursor(Output& output);
			void				_ApplyGamma(Output& output);
			void				_HideCursors();

	static	void*				_EventThreadEntry(void* data);
			void				_EventThread();
	static	void*				_PresentThreadEntry(void* data);
			void				_PresentThread();
			void				_Wake();
	static	void				_FlipHandler(int fd, unsigned int sequence,
									unsigned int seconds,
									unsigned int microseconds,
									unsigned int crtcId, void* data);
	static	void				_BlankHandler(int fd, unsigned int sequence,
									unsigned int seconds,
									unsigned int microseconds, void* data);
			void				_HandleUdev();

	// KmsSession::Listener
	virtual	void				SessionEnabled();
	virtual	void				SessionDisabled();

			uint32				fFlags;
			KmsSession			fSession;
			char				fDriverName[64];
			bool				fAtomic;
			bool				fDumb;
			uint32				fMaxWidth;
			uint32				fMaxHeight;
			uint32				fCursorWidth;
			uint32				fCursorHeight;

			std::vector<Crtc>	fCrtcs;
			std::vector<Output>	fOutputs;
			bool				fOutputsChanged;

			KmsLayout			fCurrent;		// as driven
			bool				fModeSet;
			KmsLayout			fPending;		// SetDisplayLayout()'s
			bool				fHasPending;
			display_mode		fMode;
			std::vector<Scanout*> fScanouts;
			uint32				fGeneration;	// of fScanouts
			uint32				fBlankGeneration;
			bool				fSplashQuit;

			uint32				fDPMSMode;
			float				fTemperature;
			bool				fFirstModeset;

			gbm_device*			fGbm;
			KmsGles*			fGles;
			bool				fGlesFailed;
			bool				fForceBuffers;	// VOS_KMS_BUFFERS
			bool				fUseGbmBuffers;
			int32				fSlots;			// buffers per CPU scan-out

			// cursor
			std::vector<uint32>	fCursorImage;
			uint32				fCursorImageWidth;
			uint32				fCursorImageHeight;
			int32				fCursorHotX;
			int32				fCursorHotY;
			int32				fCursorX;
			int32				fCursorY;
			bool				fCursorVisible;
			bool				fCursorOk;

			// the threads
			pthread_mutex_t		fLock;
			pthread_cond_t		fCondition;		// work, flips done
			pthread_t			fEventThread;
			pthread_t			fPresentThread;
			bool				fEventThreadRunning;
			bool				fPresentThreadRunning;
			volatile bool		fQuit;
			int					fWakeFd;
			bool				fRendering;
			bigtime_t			fRetraceWantedUntil;
			int64				fFlips;
			uint32				fFlipFailures;
			bool				fFlipBroken;

			KmsFrontSource*		fFrontSource;
			KmsDeviceListener*	fListener;

			struct udev*		fUdev;
			struct udev_monitor* fUdevMonitor;
			dev_t				fDevice;
};


#endif	// KMS_DEVICE_H
