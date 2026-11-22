/*
 * Copyright 2026, air/OS contributors.
 * Distributed under the terms of the MIT License.
 */


#include "Device.h"

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#ifdef __linux__
#	include <limits.h>
#	include <sys/mman.h>
#endif
#include <sys/stat.h>
#include <syslog.h>
#include <unistd.h>

#include <Autolock.h>
#include <Message.h>
#include <OS.h>


/*!	The node of the NPU. On Linux the amdxdna driver is a DRM accel
	driver: its node is the /dev/accel/accel<N> whose device the amdxdna
	driver is bound to, which sysfs tells without opening anything. NULL
	when there is none. */
static const char*
device_path()
{
#ifdef __linux__
	static char path[32];
	for (int i = 0; i < 16; i++) {
		char link[64];
		char target[PATH_MAX];
		snprintf(link, sizeof(link), "/sys/class/accel/accel%d/device/driver",
			i);
		ssize_t length = readlink(link, target, sizeof(target) - 1);
		if (length < 0)
			continue;
		target[length] = '\0';
		const char* driver = strrchr(target, '/');
		if (strcmp(driver != NULL ? driver + 1 : target, "amdxdna") == 0) {
			snprintf(path, sizeof(path), "/dev/accel/accel%d", i);
			return path;
		}
	}
	return NULL;
#else
	return XDNA_DEVICE_PATH;
#endif
}


Device::Device()
	:
	fLock("amdxdna device"),
	fFD(-1),
	fUsers(0),
	fKnown(false)
{
	memset(&fMetadata, 0, sizeof(fMetadata));
	memset(&fFirmware, 0, sizeof(fFirmware));
	fDriver[0] = '\0';
	fDriverVersion[0] = fDriverVersion[1] = 0;
}


Device::~Device()
{
	_Close();
}


bool
Device::Exists()
{
	// Looking is all: opening the node starts the NPU.
	const char* path = device_path();
	struct stat st;
	return path != NULL && stat(path, &st) == 0 && S_ISCHR(st.st_mode);
}


status_t
Device::Acquire()
{
	BAutolock locker(fLock);
	if (fUsers == 0) {
		status_t status = _Open();
		if (status != B_OK)
			return status;
	}
	fUsers++;
	return B_OK;
}


void
Device::Release()
{
	BAutolock locker(fLock);
	if (--fUsers == 0)
		_Close();
}


/*!	The NPU's power mode is a setting of the device, kept until changed.
	In every mode but turbo the firmware gates the NPU's clocks when they
	are idle, and on this driver every command then takes 112.5 ms to be
	answered, whatever it computes (seen on a Ryzen AI 5 340; not
	understood yet: with the clocks running a command takes 0.1 ms). So
	the back end asks for turbo, which the driver only grants while no
	context exists - hence here, before the first program's; when another
	client has a context, the mode stays what it is. NEURAL_AMDXDNA_POWER
	(0 default, 1 low, 2 medium, 3 high, 4 turbo) in the environment
	chooses another mode, for measurements. The NPU is powered down
	altogether when nobody has it open. */
void
Device::_SetPowerMode()
{
	int mode = XDNA_POWER_TURBO;
	const char* chosen = getenv("NEURAL_AMDXDNA_POWER");
	if (chosen != NULL && isdigit(chosen[0]))
		mode = atoi(chosen);
	if (mode < XDNA_POWER_DEFAULT || mode > XDNA_POWER_TURBO)
		return;

	xdna_set_power_mode power;
	memset(&power, 0, sizeof(power));
	power.power_mode = (uint8)mode;
	xdna_set_state state;
	memset(&state, 0, sizeof(state));
	state.param = XDNA_SET_POWER_MODE;
	state.buffer_size = sizeof(power);
	state.buffer = (addr_t)&power;
	if (ioctl(fFD, XDNA_IOCTL_SET_STATE, &state) != 0 && errno != EBUSY) {
		syslog(LOG_INFO, "amdxdna.neural: power mode %d not set: %s", mode,
			strerror(errno));
	}
}


status_t
Device::_Open()
{
	// The driver lets nobody else in (a client can have the NPU read and
	// write any memory), and tells so; asking first keeps the reason
	// plain.
	// On Linux the driver needs an IOMMU and the node's permissions
	// decide.
#ifndef __linux__
	if (geteuid() != 0)
		return B_PERMISSION_DENIED;
#endif
	const char* path = device_path();
	if (path == NULL)
		return B_ENTRY_NOT_FOUND;

	// This is where the NPU is powered up and gets its firmware.
	fFD = open(path, O_RDWR | O_CLOEXEC);
	if (fFD < 0) {
		status_t status = errno;
		syslog(LOG_WARNING, "amdxdna.neural: cannot open %s: %s",
			path, strerror(status));
		return status;
	}

	xdna_drm_version version;
	memset(&version, 0, sizeof(version));
	version.name = fDriver;
	version.name_len = sizeof(fDriver) - 1;
	if (ioctl(fFD, XDNA_IOCTL_VERSION, &version) == 0) {
		fDriver[sizeof(fDriver) - 1] = '\0';
		fDriverVersion[0] = version.version_major;
		fDriverVersion[1] = version.version_minor;
	} else
		fDriver[0] = '\0';

	xdna_get_info query;
	memset(&query, 0, sizeof(query));
	query.param = XDNA_QUERY_AIE_METADATA;
	query.buffer_size = sizeof(fMetadata);
	query.buffer = (addr_t)&fMetadata;
	status_t status = B_OK;
	if (ioctl(fFD, XDNA_IOCTL_GET_INFO, &query) != 0)
		status = errno;
	else if (fMetadata.cols == 0 || fMetadata.core.row_count == 0)
		status = B_BAD_DATA;

	if (status == B_OK) {
		query.param = XDNA_QUERY_FIRMWARE_VERSION;
		query.buffer_size = sizeof(fFirmware);
		query.buffer = (addr_t)&fFirmware;
		if (ioctl(fFD, XDNA_IOCTL_GET_INFO, &query) != 0)
			memset(&fFirmware, 0, sizeof(fFirmware));
		fKnown = true;
		_SetPowerMode();

		// the heap, which has to be mapped before anything is put in it
		status = CreateBuffer(XDNA_BO_DEV_HEAP, XDNA_HEAP_SIZE, fHeap);
	}

	if (status != B_OK) {
		syslog(LOG_WARNING, "amdxdna.neural: the NPU is of no use: %s",
			strerror(status));
		_Close();
	}
	return status;
}


void
Device::_Close()
{
	if (fFD < 0)
		return;
	DeleteBuffer(fHeap);
	// The last close: the driver powers the NPU down.
	close(fFD);
	fFD = -1;
}


void
Device::GetDetails(BMessage* details)
{
	BAutolock locker(fLock);
	const char* path = device_path();
	if (path != NULL)
		details->AddString("path", path);
	details->AddString("driver", "amdxdna");
	if (!fKnown)
		return;

	char text[128];
	if (fDriver[0] != '\0') {
		snprintf(text, sizeof(text), "%s %" B_PRId32 ".%" B_PRId32, fDriver,
			fDriverVersion[0], fDriverVersion[1]);
		details->AddString("driver interface", text);
	}
	snprintf(text, sizeof(text), "%" B_PRIu32 ".%" B_PRIu32 ".%" B_PRIu32
		".%" B_PRIu32, fFirmware.major, fFirmware.minor, fFirmware.patch,
		fFirmware.build);
	details->AddString("version", text);
	details->AddInt32("columns", fMetadata.cols);
	details->AddInt32("rows", fMetadata.rows);
	details->AddInt32("cores", fMetadata.cols * fMetadata.core.row_count);
	snprintf(text, sizeof(text), "%" B_PRIu32 ".%" B_PRIu32,
		fMetadata.version.major, fMetadata.version.minor);
	details->AddString("aie version", text);
}


status_t
Device::CreateBuffer(uint32 type, size_t size, Buffer& buffer)
{
	buffer = Buffer();
	if (fFD < 0)
		return B_NO_INIT;

	xdna_create_bo create;
	memset(&create, 0, sizeof(create));
	create.size = size;
	create.type = type;
	if (ioctl(fFD, XDNA_IOCTL_CREATE_BO, &create) != 0)
		return errno;
	buffer.handle = create.handle;
	buffer.type = type;
	buffer.size = size;

	xdna_get_bo_info info;
	memset(&info, 0, sizeof(info));
	info.handle = buffer.handle;
	status_t status = B_OK;
	if (ioctl(fFD, XDNA_IOCTL_GET_BO_INFO, &info) != 0)
		status = errno;

	if (status == B_OK && type == XDNA_BO_DEV) {
		// a part of the heap, at the heap's addresses
		if (fHeap.data == NULL || info.xdna_addr < fHeap.address
			|| info.xdna_addr + size > fHeap.address + fHeap.size) {
			status = B_BAD_DATA;
		} else {
			buffer.address = info.xdna_addr;
			buffer.data = (uint8*)fHeap.data + (info.xdna_addr - fHeap.address);
			memset(buffer.data, 0, size);
		}
	} else if (status == B_OK) {
#ifdef __linux__
		// Linux maps a buffer with mmap() at its map_offset.
		size_t mapSize = (size + B_PAGE_SIZE - 1) & ~((size_t)B_PAGE_SIZE - 1);
		void* data = mmap(NULL, mapSize, PROT_READ | PROT_WRITE, MAP_SHARED,
			fFD, (off_t)info.map_offset);
		if (data == MAP_FAILED) {
			status = errno;
		} else {
			buffer.data = data;
			buffer.mapped = true;
			// the address is known once the buffer is mapped
			if (ioctl(fFD, XDNA_IOCTL_GET_BO_INFO, &info) != 0)
				status = errno;
			else if (info.xdna_addr == XDNA_INVALID_ADDRESS)
				status = B_BAD_DATA;
			buffer.address = info.xdna_addr;
		}
#else
		xdna_map map;
		memset(&map, 0, sizeof(map));
		map.offset = info.map_offset;
		map.size = (size + B_PAGE_SIZE - 1) & ~((size_t)B_PAGE_SIZE - 1);
		map.flags = XDNA_MAP_WRITE;
		if (ioctl(fFD, XDNA_IOCTL_MAP, &map) != 0) {
			status = errno;
		} else {
			buffer.data = (void*)(addr_t)map.address;
			buffer.mapped = true;
			// the address is known once the buffer is mapped
			if (ioctl(fFD, XDNA_IOCTL_GET_BO_INFO, &info) != 0)
				status = errno;
			else if (info.xdna_addr == XDNA_INVALID_ADDRESS)
				status = B_BAD_DATA;
			buffer.address = info.xdna_addr;
		}
#endif
	}

	if (status != B_OK)
		DeleteBuffer(buffer);
	return status;
}


void
Device::DeleteBuffer(Buffer& buffer)
{
	if (fFD >= 0 && buffer.mapped) {
#ifdef __linux__
		munmap(buffer.data,
			(buffer.size + B_PAGE_SIZE - 1) & ~((size_t)B_PAGE_SIZE - 1));
#else
		xdna_unmap unmap;
		unmap.address = (addr_t)buffer.data;
		ioctl(fFD, XDNA_IOCTL_UNMAP, &unmap);
#endif
	}
	if (fFD >= 0 && buffer.handle != 0) {
		xdna_gem_close close;
		close.handle = buffer.handle;
		close.pad = 0;
		ioctl(fFD, XDNA_IOCTL_GEM_CLOSE, &close);
	}
	buffer = Buffer();
}


void
Device::Flush(const Buffer& buffer)
{
	if (buffer.data == NULL)
		return;

	// The lines out of the processor's caches, which is what the driver's
	// sync ioctl does where it has one: without an IOMMU all buffers are
	// of the driver's contiguous memory, and for those Linux's driver
	// offers none (and says so in the log at every call).
	__builtin_ia32_mfence();
	for (size_t offset = 0; offset < buffer.size; offset += 64)
		__builtin_ia32_clflush((uint8*)buffer.data + offset);
	__builtin_ia32_mfence();
}
