/*
 * Copyright 2026, Koan contributors. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef KMS_SESSION_H
#define KMS_SESSION_H


#include <SupportDefs.h>


struct libseat;


/*!	How app_server gets a DRM device and becomes its master, as VitruvianOS's
	DRM backend does:
	- janus_session hands it an open, master descriptor (JANUS_DRM_FD);
	- else a libseat session (logind or seatd) opens the card and follows
	  the VT: disable drops master, enable takes it back;
	- else (a test harness as root, no seat manager) the card is opened
	  directly and drmSetMaster() is asked for.
	VOS_KMS_SEAT=none skips libseat.
*/
class KmsSession {
public:
	class Listener {
	public:
		virtual					~Listener() {}
		virtual	void			SessionEnabled() = 0;
		virtual	void			SessionDisabled() = 0;
	};

								KmsSession();
								~KmsSession();

			// \a path NULL: the card that drives a display (an internal
			// panel first, then a connected monitor, then boot_vga)
			status_t			Open(const char* path, Listener* listener);
			void				Close();

			int					Fd() const { return fFd; }
			const char*			Path() const { return fPath; }
			bool				IsActive() const { return fActive; }
			const char*			How() const { return fHow; }

			// for the device's event loop: -1 without a seat
			int					DispatchFd() const;
			void				Dispatch();

			// the seat is being left (VT switch): after the caller stopped
			// using the device
			void				AcknowledgeDisable();
			// janus_session switches the seat itself and says so
			void				SetActive(bool active) { fActive = active; }

private:
	static	void				_EnableSeat(struct libseat* seat, void* data);
	static	void				_DisableSeat(struct libseat* seat,
									void* data);
			status_t			_OpenSeat(const char* path);
			status_t			_OpenDirect(const char* path);

			int					fFd;
			char				fPath[64];
			const char*			fHow;
			bool				fActive;
			bool				fOwnsFd;
			Listener*			fListener;
			struct libseat*		fSeat;
			int					fSeatDevice;
};


#endif	// KMS_SESSION_H
