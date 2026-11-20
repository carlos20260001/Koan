/*
 * Copyright 2026, Koan contributors. All rights reserved.
 * Distributed under the terms of the MIT License.
 */


#include "KmsSession.h"

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <time.h>
#include <unistd.h>

#include <xf86drm.h>
#include <xf86drmMode.h>

#ifdef VOS_HAVE_KMS_LIBSEAT
// libseat.h has no C++ guards of its own
extern "C" {
#	include <libseat.h>
}
#	include <device/DrmDeviceSelect.h>
#endif


#define TRACE(x...) fprintf(stderr, "kms: " x)


static status_t
errno_status()
{
	return errno > 0 ? -errno : B_ERROR;
}


// #pragma mark - choosing a card without a seat


static bool
card_is_firmware(int index)
{
	char path[64];
	snprintf(path, sizeof(path), "/sys/class/drm/card%d/device/driver", index);
	char target[256];
	ssize_t length = readlink(path, target, sizeof(target) - 1);
	if (length <= 0)
		return false;
	target[length] = '\0';
	const char* name = strrchr(target, '/');
	name = name != NULL ? name + 1 : target;
	return strcmp(name, "simple-framebuffer") == 0
		|| strcmp(name, "simpledrm") == 0
		|| strcmp(name, "efi-framebuffer") == 0
		|| strcmp(name, "efidrm") == 0;
}


static bool
card_is_boot_vga(int index)
{
	char path[64];
	snprintf(path, sizeof(path), "/sys/class/drm/card%d/device/boot_vga",
		index);
	int fd = open(path, O_RDONLY | O_CLOEXEC);
	if (fd < 0)
		return false;
	char buffer[4] = {};
	ssize_t length = read(fd, buffer, sizeof(buffer) - 1);
	close(fd);
	return length > 0 && atoi(buffer) == 1;
}


/*!	As DrmDeviceSelect's drm_card_display_rank(): an internal panel first,
	a connected monitor next, boot_vga breaks ties, a card without
	connectors (a render-only GPU) not at all. */
static int
card_rank(int fd, int index)
{
	drmModeRes* resources = drmModeGetResources(fd);
	if (resources == NULL)
		return -1;
	bool connected = false, internal = false;
	bool hasConnectors = resources->count_connectors > 0;
	for (int i = 0; i < resources->count_connectors; i++) {
		drmModeConnector* connector = drmModeGetConnectorCurrent(fd,
			resources->connectors[i]);
		if (connector == NULL)
			continue;
		if (connector->connection == DRM_MODE_CONNECTED)
			connected = true;
		if (connector->connector_type == DRM_MODE_CONNECTOR_eDP
			|| connector->connector_type == DRM_MODE_CONNECTOR_LVDS
			|| connector->connector_type == DRM_MODE_CONNECTOR_DSI)
			internal = true;
		drmModeFreeConnector(connector);
	}
	drmModeFreeResources(resources);
	bool bootVga = card_is_boot_vga(index);
	if (bootVga && internal)
		return 9;
	if (connected && internal)
		return 7;
	if (connected && hasConnectors)
		return bootVga ? 6 : 5;
	if (bootVga && hasConnectors)
		return 3;
	if (hasConnectors)
		return 1;
	return -1;
}


// #pragma mark - KmsSession


KmsSession::KmsSession()
	:
	fFd(-1),
	fHow("none"),
	fActive(false),
	fOwnsFd(false),
	fListener(NULL),
	fSeat(NULL),
	fSeatDevice(-1)
{
	fPath[0] = '\0';
}


KmsSession::~KmsSession()
{
	Close();
}


status_t
KmsSession::Open(const char* path, Listener* listener)
{
	fListener = listener;

	const char* janus = getenv("JANUS_DRM_FD");
	if (janus != NULL && janus[0] != '\0') {
		fFd = atoi(janus);
		struct stat st;
		if (fFd < 0 || fstat(fFd, &st) != 0)
			return B_BAD_VALUE;
		snprintf(fPath, sizeof(fPath), "/dev/dri/card%d",
			(int)(minor(st.st_rdev)));
		fHow = "janus_session";
		fActive = true;
		fOwnsFd = false;
		return B_OK;
	}

	const char* seat = getenv("VOS_KMS_SEAT");
	if (seat == NULL || strcmp(seat, "none") != 0) {
		if (_OpenSeat(path) == B_OK)
			return B_OK;
	}
	return _OpenDirect(path);
}


void
KmsSession::Close()
{
#ifdef VOS_HAVE_KMS_LIBSEAT
	if (fSeat != NULL) {
		if (fSeatDevice >= 0)
			libseat_close_device(fSeat, fSeatDevice);
		libseat_close_seat(fSeat);
		fSeat = NULL;
		fSeatDevice = -1;
		fFd = -1;
	}
#endif
	if (fFd >= 0 && fOwnsFd) {
		drmDropMaster(fFd);
		close(fFd);
	}
	fFd = -1;
	fActive = false;
}


int
KmsSession::DispatchFd() const
{
#ifdef VOS_HAVE_KMS_LIBSEAT
	if (fSeat != NULL)
		return libseat_get_fd(fSeat);
#endif
	return -1;
}


void
KmsSession::Dispatch()
{
#ifdef VOS_HAVE_KMS_LIBSEAT
	if (fSeat != NULL)
		libseat_dispatch(fSeat, 0);
#endif
}


void
KmsSession::AcknowledgeDisable()
{
#ifdef VOS_HAVE_KMS_LIBSEAT
	if (fSeat != NULL)
		libseat_disable_seat(fSeat);
#endif
}


/*static*/ void
KmsSession::_EnableSeat(struct libseat* seat, void* data)
{
	KmsSession* session = (KmsSession*)data;
	session->fActive = true;
	if (session->fListener != NULL && session->fFd >= 0)
		session->fListener->SessionEnabled();
}


/*static*/ void
KmsSession::_DisableSeat(struct libseat* seat, void* data)
{
	KmsSession* session = (KmsSession*)data;
	session->fActive = false;
	if (session->fListener != NULL && session->fFd >= 0)
		session->fListener->SessionDisabled();
	else
		session->AcknowledgeDisable();
}


status_t
KmsSession::_OpenSeat(const char* path)
{
#ifdef VOS_HAVE_KMS_LIBSEAT
	static struct libseat_seat_listener listener = {
		_EnableSeat,
		_DisableSeat
	};
	fSeat = libseat_open_seat(&listener, this);
	if (fSeat == NULL)
		return B_ERROR;
	// a seat that never activates must not hang app_server
	for (int waited = 0; !fActive && waited < 3000; waited += 100) {
		if (libseat_dispatch(fSeat, 100) < 0)
			break;
	}
	if (!fActive) {
		TRACE("libseat session did not activate\n");
		libseat_close_seat(fSeat);
		fSeat = NULL;
		return B_ERROR;
	}

	int device = -1, fd = -1, index = -1;
	const char* why = NULL;
	if (path != NULL) {
		device = libseat_open_device(fSeat, path, &fd);
		why = "asked for";
		strlcpy(fPath, path, sizeof(fPath));
	} else if (drm_select_seat_device(fSeat, device, fd, index, why))
		snprintf(fPath, sizeof(fPath), "/dev/dri/card%d", index);
	if (device < 0 || fd < 0) {
		libseat_close_seat(fSeat);
		fSeat = NULL;
		return B_ENTRY_NOT_FOUND;
	}
	fSeatDevice = device;
	fFd = fd;
	fOwnsFd = false;
	fHow = "libseat";
	TRACE("%s through libseat (%s)\n", fPath, why);
	return B_OK;
#else
	return B_NOT_SUPPORTED;
#endif
}


status_t
KmsSession::_OpenDirect(const char* path)
{
	int best = -1, bestRank = -1, bestIndex = -1;
	for (int i = 0; i <= 15; i++) {
		char candidate[64];
		if (path != NULL) {
			if (i > 0)
				break;
			strlcpy(candidate, path, sizeof(candidate));
		} else {
			if (card_is_firmware(i))
				continue;
			snprintf(candidate, sizeof(candidate), "/dev/dri/card%d", i);
		}
		int fd = open(candidate, O_RDWR | O_CLOEXEC);
		if (fd < 0)
			continue;
		int rank = path != NULL ? 0 : card_rank(fd, i);
		if (rank > bestRank) {
			if (best >= 0)
				close(best);
			best = fd;
			bestRank = rank;
			bestIndex = i;
			strlcpy(fPath, candidate, sizeof(fPath));
		} else
			close(fd);
	}
	if (best < 0)
		return B_ENTRY_NOT_FOUND;
	(void)bestIndex;

	// the first opener is master already; else ask (root, or CAP_SYS_ADMIN)
	if (drmSetMaster(best) != 0 && !drmIsMaster(best)) {
		status_t status = errno_status();
		TRACE("%s: not the DRM master: %s\n", fPath, strerror(errno));
		close(best);
		return status;
	}
	fFd = best;
	fOwnsFd = true;
	fActive = true;
	fHow = "direct";
	TRACE("%s opened directly\n", fPath);
	return B_OK;
}
