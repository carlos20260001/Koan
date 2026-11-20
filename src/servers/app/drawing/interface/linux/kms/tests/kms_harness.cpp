/*
 * Copyright 2026, Koan contributors. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * Drives the KMS backend (KmsDevice) on a real or virtual KMS device
 * without app_server: enumerates the outputs, sets layouts on them at
 * render scales, draws a known picture into a frame buffer, flips it out
 * and reads the scan-out buffers back; vertical blanks, scan lines, DPMS,
 * the cursor planes, mirroring. For QEMU (vkms, virtio-gpu); as root.
 *
 *	kms_harness [--device /dev/dri/cardN] [--scales 100,125,150,175]
 *		[--frames N]
 */


#include "KmsBlit.h"
#include "KmsDevice.h"
#include "KmsEdid.h"
#include "KmsLayout.h"

#include <algorithm>
#include <math.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <vector>


static int sFailures = 0;
static int sPasses = 0;


static void
result(bool ok, const char* format, ...)
{
	va_list args;
	va_start(args, format);
	printf("%s: ", ok ? "PASS" : "FAIL");
	vprintf(format, args);
	printf("\n");
	va_end(args);
	fflush(stdout);
	if (ok)
		sPasses++;
	else
		sFailures++;
}


/*!	The frame buffer app_server would draw into, behind a read-write lock
	as app_server's MultiLocker. */
class Front : public KmsFrontSource {
public:
	Front()
		:
		fWidth(0),
		fHeight(0)
	{
		pthread_rwlock_init(&fLock, NULL);
	}

	virtual bool LockFront(KmsFrontView& view)
	{
		pthread_rwlock_rdlock(&fLock);
		if (fPixels.empty()) {
			pthread_rwlock_unlock(&fLock);
			return false;
		}
		view.bits = (const uint8*)&fPixels[0];
		view.bytesPerRow = fWidth * 4;
		view.width = fWidth;
		view.height = fHeight;
		return true;
	}

	virtual void UnlockFront()
	{
		pthread_rwlock_unlock(&fLock);
	}

	void WriteLock() { pthread_rwlock_wrlock(&fLock); }
	void WriteUnlock() { pthread_rwlock_unlock(&fLock); }

	void Resize(uint32 width, uint32 height)
	{
		fWidth = width;
		fHeight = height;
		fPixels.assign((size_t)width * height, 0xff000000);
	}

	uint32& At(int32 x, int32 y) { return fPixels[(size_t)y * fWidth + x]; }

	std::vector<uint32>	fPixels;
	uint32				fWidth;
	uint32				fHeight;
	pthread_rwlock_t	fLock;
};


class Listener : public KmsDeviceListener {
public:
	Listener() : fChanges(0), fRetraces(0) {}
	virtual void KmsOutputsChanged() { __atomic_add_fetch(&fChanges, 1,
		__ATOMIC_SEQ_CST); }
	virtual void KmsRetrace(uint32 id, bigtime_t when) { fRetraces++; }
	int32 fChanges;
	int64 fRetraces;
};


/*!	Tiles of flat colour, 48 frame buffer pixels a side, with a seed. */
static uint32
tile_color(int32 x, int32 y, uint32 seed)
{
	uint32 tx = x / 48, ty = y / 48;
	uint32 value = (tx * 73856093u) ^ (ty * 19349663u) ^ (seed * 83492791u);
	return 0xff000000 | (value & 0x00ffffff);
}


static void
draw(Front& front, KmsDevice& device, uint32 seed, const KmsRect& area)
{
	front.WriteLock();
	for (int32 y = area.y; y < area.Bottom(); y++) {
		for (int32 x = area.x; x < area.Right(); x++)
			front.At(x, y) = tile_color(x, y, seed);
	}
	front.WriteUnlock();
	device.Damage(area);
}


/*!	Compares what an output shows with what it should: tile interiors
	(colour kept by any filter: the GPU's too) everywhere, and every pixel
	against the CPU's resample of the same frame buffer when \a exact. */
static bool
check_output(Front& front, KmsDevice& device, const display_output& output,
	bool exact, const char* what)
{
	std::vector<uint32> pixels;
	uint32 width = 0, height = 0;
	KmsRect destination;
	status_t status = device.ReadBack(output.id, pixels, &width, &height,
		&destination);
	if (status != B_OK) {
		result(false, "%s: %s: read back: %s", what, output.name,
			strerror(-status));
		return false;
	}
	KmsRect region(output.x, output.y, output.width, output.height);
	bool scaled = width != (uint32)region.width
		|| height != (uint32)region.height;

	// the reference: the same copy or resample on the CPU
	std::vector<uint32> expected((size_t)width * height, 0xff000000);
	const uint8* source = (const uint8*)&front.fPixels[0]
		+ ((size_t)region.y * front.fWidth + region.x) * 4;
	if (scaled) {
		KmsScaler scaler;
		scaler.SetTo(region.width, region.height, destination.width,
			destination.height);
		scaler.Scale(source, front.fWidth * 4, (uint8*)&expected[0]
				+ ((size_t)destination.y * width + destination.x) * 4,
			width * 4, KmsRect(0, 0, destination.width, destination.height),
			true);
	} else {
		kms_copy_rect(source, front.fWidth * 4, (uint8*)&expected[0],
			width * 4, KmsRect(0, 0, width, height), 0, 0, true);
	}

	uint32 differ = 0, tileMisses = 0, tilePixels = 0;
	int32 firstX = -1, firstY = -1;
	double fx = (double)region.width / destination.width;
	double fy = (double)region.height / destination.height;
	for (uint32 y = 0; y < height; y++) {
		for (uint32 x = 0; x < width; x++) {
			uint32 got = pixels[(size_t)y * width + x] | 0xff000000;
			uint32 want = expected[(size_t)y * width + x];
			if (exact && got != want) {
				if (differ++ == 0) {
					firstX = x;
					firstY = y;
				}
			}
			// well inside a tile (3 source pixels from its edges)
			if (!destination.Contains(x, y))
				continue;
			double sx = region.x + (x - destination.x + 0.5) * fx;
			double sy = region.y + (y - destination.y + 0.5) * fy;
			int32 ix = (int32)sx, iy = (int32)sy;
			if (ix % 48 < 3 || ix % 48 > 44 || iy % 48 < 3 || iy % 48 > 44)
				continue;
			if (ix >= region.Right() - 3 || iy >= region.Bottom() - 3)
				continue;
			tilePixels++;
			uint32 tile = front.At(ix, iy) | 0xff000000;
			int32 delta = 0;
			for (int c = 0; c < 24; c += 8) {
				delta = std::max(delta, abs((int32)((got >> c) & 0xff)
					- (int32)((tile >> c) & 0xff)));
			}
			if (delta > 2)
				tileMisses++;
		}
	}
	bool ok = differ == 0 && tileMisses == 0 && tilePixels > 0;
	result(ok, "%s: %s: %ux%u buffer, region %d,%d %dx%d%s, %u pixels off "
		"the CPU reference%s, %u of %u tile pixels off", what, output.name,
		width, height, region.x, region.y, region.width, region.height,
		scaled ? " (resampled)" : "", differ, exact ? "" : " (not compared)",
		tileMisses, tilePixels);
	if (differ > 0) {
		printf("      first at %d,%d: got 0x%08x, want 0x%08x\n", firstX,
			firstY, pixels[(size_t)firstY * width + firstX] | 0xff000000,
			expected[(size_t)firstY * width + firstX]);
	}
	return ok;
}


static status_t
set_layout(KmsDevice& device, Front& front, std::vector<display_output>&
	outputs, uint16 renderScale, bool mirror, display_mode& mode)
{
	std::vector<display_output_config> configs;
	int32 x = 0;
	for (size_t i = 0; i < outputs.size(); i++) {
		const display_output& output = outputs[i];
		if ((output.flags & B_DISPLAY_OUTPUT_CONNECTED) == 0)
			continue;
		display_output_config config;
		memset(&config, 0, sizeof(config));
		config.id = output.id;
		config.flags = B_DISPLAY_OUTPUT_ENABLED;
		if (mirror && !configs.empty())
			config.flags |= B_DISPLAY_OUTPUT_MIRROR;
		config.scale = 100;
		config.render_scale = renderScale;
		config.timing = output.native_timing;
		config.x = mirror ? 0 : x;
		config.y = 0;
		x += output.native_timing.h_display;
		configs.push_back(config);
	}
	if (configs.empty())
		return B_ENTRY_NOT_FOUND;
	status_t status = device.SetDisplayLayout(&configs[0], configs.size(),
		&mode);
	if (status != B_OK)
		return status;
	front.WriteLock();
	status = device.SetMode(&mode);
	if (status == B_OK)
		front.Resize(mode.virtual_width, mode.virtual_height);
	front.WriteUnlock();
	return status;
}


static std::vector<display_output>
get_outputs(KmsDevice& device)
{
	display_output* list = NULL;
	uint32 count = 0;
	std::vector<display_output> outputs;
	if (device.GetDisplayOutputs(&list, &count) == B_OK) {
		outputs.assign(list, list + count);
		free(list);
	}
	return outputs;
}


int
main(int argc, char** argv)
{
	const char* path = NULL;
	std::vector<uint16> scales;
	int frames = 120;
	for (int i = 1; i < argc; i++) {
		if (strcmp(argv[i], "--device") == 0 && i + 1 < argc)
			path = argv[++i];
		else if (strcmp(argv[i], "--frames") == 0 && i + 1 < argc)
			frames = atoi(argv[++i]);
		else if (strcmp(argv[i], "--scales") == 0 && i + 1 < argc) {
			char* list = strdup(argv[++i]);
			for (char* item = strtok(list, ","); item != NULL;
					item = strtok(NULL, ","))
				scales.push_back(atoi(item));
			free(list);
		}
	}
	if (scales.empty()) {
		scales.push_back(100);
		scales.push_back(125);
		scales.push_back(150);
		scales.push_back(175);
	}
	setenv("VOS_KMS_SEAT", "none", 0);

	status_t status;
	KmsDevice* device = KmsDevice::Open(path, 0, &status);
	if (device == NULL) {
		result(false, "open: %s", strerror(-status));
		return 1;
	}
	Front front;
	Listener listener;
	device->SetFrontSource(&front);
	device->SetListener(&listener);
	bool exact = !device->UsesGpu();
	printf("device %s, driver %s, %s, %s path%s%s\n", device->Path(),
		device->DriverName(), device->IsAtomic() ? "atomic" : "legacy",
		device->UsesGpu() ? "GPU" : "CPU",
		device->UsesGpu() ? ": " : "", device->GpuRenderer());

	// the outputs
	std::vector<display_output> outputs = get_outputs(*device);
	int connected = 0;
	for (size_t i = 0; i < outputs.size(); i++) {
		const display_output& output = outputs[i];
		bool on = (output.flags & B_DISPLAY_OUTPUT_CONNECTED) != 0;
		connected += on ? 1 : 0;
		monitor_info info;
		bool edid = output.edid_length > 0 && kms_edid_monitor_info(
			output.edid, output.edid_length, &info) == B_OK;
		display_mode* modes = NULL;
		uint32 modeCount = 0;
		device->GetDisplayOutputModes(output.id, &modes, &modeCount);
		free(modes);
		printf("output %u %s: %s, %u modes, native %ux%u, EDID %s%s%s\n",
			output.id, output.name, on ? "connected" : "disconnected",
			modeCount, output.native_timing.h_display,
			output.native_timing.v_display,
			edid ? "\"" : output.edid_length > 0 ? "(undecoded)" : "none",
			edid ? info.vendor : "", edid ? "\"" : "");
	}
	result(connected > 0, "%zu outputs, %d connected", outputs.size(),
		connected);
	if (connected == 0)
		return 1;

	display_mode mode;
	uint32 seed = 1;
	for (size_t s = 0; s < scales.size(); s++) {
		uint16 scale = scales[s];
		status = set_layout(*device, front, outputs, scale, false, mode);
		result(status == B_OK, "layout at %u%%: %ux%u frame buffer: %s",
			scale, mode.virtual_width, mode.virtual_height,
			strerror(-status));
		if (status != B_OK)
			continue;
		outputs = get_outputs(*device);
		draw(front, *device, seed++, KmsRect(0, 0, front.fWidth,
			front.fHeight));
		result(device->WaitIdle(2000000) == B_OK, "flipped out at %u%%",
			scale);
		for (size_t i = 0; i < outputs.size(); i++) {
			if ((outputs[i].flags & B_DISPLAY_OUTPUT_ENABLED) != 0)
				check_output(front, *device, outputs[i], exact, "full frame");
		}

		// partial updates: the ring's buffers stay complete
		for (int i = 0; i < 7; i++) {
			// on the tile grid, so that tiles stay flat
			int32 w = 48 * (1 + i % 4), h = 48 * (1 + (i * 3) % 3);
			int32 x = 48 * ((i * 5) % std::max(1, (int32)front.fWidth / 48
				- 4));
			int32 y = 48 * ((i * 3) % std::max(1, (int32)front.fHeight / 48
				- 3));
			draw(front, *device, seed++, KmsRect(x, y, w, h));
			device->WaitIdle(1000000);
		}
		for (size_t i = 0; i < outputs.size(); i++) {
			if ((outputs[i].flags & B_DISPLAY_OUTPUT_ENABLED) != 0)
				check_output(front, *device, outputs[i], exact, "after damage");
		}
	}

	// flips and blanks at 100 %
	status = set_layout(*device, front, outputs, 100, false, mode);
	outputs = get_outputs(*device);
	draw(front, *device, seed++, KmsRect(0, 0, front.fWidth, front.fHeight));
	device->WaitIdle(2000000);
	int64 flipsBefore = device->FlipCount();
	bigtime_t start = kms_monotonic_time();
	for (int i = 0; i < frames; i++) {
		draw(front, *device, seed++, KmsRect(i % 200, 0, 64, 64));
		device->WaitIdle(500000);
	}
	bigtime_t took = kms_monotonic_time() - start;
	int64 flips = device->FlipCount() - flipsBefore;
	result(flips >= frames, "%d frames: %lld flips in %.1f ms (%.1f ms each)",
		frames, (long long)flips, took / 1000.0,
		flips > 0 ? took / 1000.0 / flips : 0.0);

	for (size_t i = 0; i < outputs.size(); i++) {
		const display_output& output = outputs[i];
		if ((output.flags & B_DISPLAY_OUTPUT_ENABLED) == 0)
			continue;
		bigtime_t before = kms_monotonic_time();
		int waited = 0;
		for (int b = 0; b < 30; b++) {
			if (device->WaitForRetrace(output.id, 100000) == B_OK)
				waited++;
		}
		bigtime_t period = (kms_monotonic_time() - before) / std::max(waited, 1);
		bigtime_t expected = 0;
		if (output.timing.h_total != 0 && output.timing.v_total != 0) {
			expected = (bigtime_t)output.timing.h_total * output.timing.v_total
				* 1000 / output.timing.pixel_clock;
		}
		result(waited == 30 && llabs(period - expected) < expected / 5 + 1000,
			"%s: 30 blanks waited for, %.2f ms apart (mode: %.2f ms)",
			output.name, period / 1000.0, expected / 1000.0);
		int32 line = 0, lines = 0;
		bigtime_t scanStart = 0, framePeriod = 0;
		status = device->ScanLine(output.id, &line, &lines, &scanStart,
			&framePeriod);
		if (status == B_UNSUPPORTED) {
			printf("SKIP: %s: no scan line (the driver has no vertical "
				"blank events)\n", output.name);
			continue;
		}
		result(status == B_OK && lines == output.height && line < lines
				&& line > -(int32)lines,
			"%s: scan line %d of %d, frame began %lld us ago, period %lld us",
			output.name, line, lines,
			(long long)(kms_monotonic_time() - scanStart),
			(long long)framePeriod);
	}

	// mirroring: every monitor shows the first one's region
	if (connected >= 2) {
		status = set_layout(*device, front, outputs, 100, true, mode);
		result(status == B_OK, "mirrored layout: %ux%u: %s",
			mode.virtual_width, mode.virtual_height, strerror(-status));
		if (status == B_OK) {
			outputs = get_outputs(*device);
			draw(front, *device, seed++, KmsRect(0, 0, front.fWidth,
				front.fHeight));
			device->WaitIdle(2000000);
			for (size_t i = 0; i < outputs.size(); i++) {
				if ((outputs[i].flags & B_DISPLAY_OUTPUT_ENABLED) != 0)
					check_output(front, *device, outputs[i], exact, "mirror");
			}
		}
		status = set_layout(*device, front, outputs, 100, false, mode);
		outputs = get_outputs(*device);
	}

	// power
	status = device->SetDPMSMode(B_DPMS_OFF);
	result(status == B_OK, "DPMS off: %s", strerror(-status));
	draw(front, *device, seed++, KmsRect(0, 0, front.fWidth, front.fHeight));
	usleep(100000);
	status = device->SetDPMSMode(B_DPMS_ON);
	result(status == B_OK, "DPMS on: %s", strerror(-status));
	result(device->WaitIdle(2000000) == B_OK, "redrawn after DPMS on");
	for (size_t i = 0; i < outputs.size(); i++) {
		if ((outputs[i].flags & B_DISPLAY_OUTPUT_ENABLED) != 0)
			check_output(front, *device, outputs[i], exact, "after DPMS");
	}

	// the cursor planes, at 100 and 150 %
	std::vector<uint32> cursor(24 * 24, 0);
	for (int y = 0; y < 24; y++) {
		for (int x = 0; x <= y; x++)
			cursor[y * 24 + x] = 0xff000000 | (x == y ? 0xffffff : 0);
	}
	device->ShowCursor(true);
	device->MoveCursor(100, 100);
	status = device->SetCursor((const uint8*)&cursor[0], 24 * 4, 24, 24, 1, 1);
	result(status == B_OK && device->HasCursor(), "hardware cursor: %s",
		strerror(-status));
	for (int i = 0; i < 50; i++)
		device->MoveCursor(100 + i * 7, 100 + i * 3);
	status = set_layout(*device, front, outputs, 150, false, mode);
	status = device->SetCursor((const uint8*)&cursor[0], 24 * 4, 24, 24, 1, 1);
	result(status == B_OK, "hardware cursor at 150%%: %s", strerror(-status));

	// hot plug: nothing changed
	result(!device->CheckOutputs(), "connectors probed again: no change");
	result(listener.fRetraces > 0, "%lld retraces told to the listener",
		(long long)listener.fRetraces);

	if (getenv("KMS_HARNESS_HOTPLUG") != NULL) {
		// the host changes the monitors now (QEMU: the second head)
		int32 before = connected;
		printf("HOTPLUG-READY\n");
		fflush(stdout);
		bigtime_t until = kms_monotonic_time() + 30000000;
		while (__atomic_load_n(&listener.fChanges, __ATOMIC_SEQ_CST) == 0
			&& kms_monotonic_time() < until)
			usleep(50000);
		outputs = get_outputs(*device);
		int32 now = 0;
		for (size_t i = 0; i < outputs.size(); i++) {
			if ((outputs[i].flags & B_DISPLAY_OUTPUT_CONNECTED) != 0)
				now++;
		}
		result(listener.fChanges > 0 && now != before,
			"hot plug event: %d connected before, %d after", before, now);
		status = set_layout(*device, front, outputs, 125, false, mode);
		result(status == B_OK, "layout on the new monitors: %ux%u: %s",
			mode.virtual_width, mode.virtual_height, strerror(-status));
		if (status == B_OK) {
			outputs = get_outputs(*device);
			draw(front, *device, seed++, KmsRect(0, 0, front.fWidth,
				front.fHeight));
			device->WaitIdle(2000000);
			for (size_t i = 0; i < outputs.size(); i++) {
				if ((outputs[i].flags & B_DISPLAY_OUTPUT_ENABLED) != 0) {
					check_output(front, *device, outputs[i], exact,
						"after hot plug");
				}
			}
		}
	}

	// a gamma ramp
	status = device->SetTemperature(4500);
	printf("colour temperature 4500 K: %s\n", strerror(-status));
	device->SetTemperature(6500);

	delete device;
	printf("kms_harness: %d passed, %d failed\n", sPasses, sFailures);
	return sFailures == 0 ? 0 : 1;
}
