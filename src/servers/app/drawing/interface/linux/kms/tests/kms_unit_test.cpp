/*
 * Copyright 2026, VitruvianOS contributors.
 * Distributed under the terms of the MIT License.
 *
 * Host tests of the KMS backend's pure parts: modes, EDID, layouts, the
 * render scale's geometry and the CPU resample. No DRM device needed.
 */


#include "KmsBlit.h"
#include "KmsEdid.h"
#include "KmsLayout.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <vector>


static int sChecks = 0;
static int sFailures = 0;

#define CHECK(condition) \
	do { \
		sChecks++; \
		if (!(condition)) { \
			sFailures++; \
			fprintf(stderr, "%s:%d: FAILED: %s\n", __FILE__, __LINE__, \
				#condition); \
		} \
	} while (0)

#define CHECK_EQ(a, b) \
	do { \
		sChecks++; \
		long long _a = (long long)(a), _b = (long long)(b); \
		if (_a != _b) { \
			sFailures++; \
			fprintf(stderr, "%s:%d: FAILED: %s == %s (%lld != %lld)\n", \
				__FILE__, __LINE__, #a, #b, _a, _b); \
		} \
	} while (0)


static const uint16 kScales[] = { 100, 125, 150, 175 };


// CVT-ish 60 Hz modes, enough for the layout's arithmetic
static drmModeModeInfo
make_mode(uint16 width, uint16 height, uint32 refresh, bool preferred)
{
	drmModeModeInfo mode;
	memset(&mode, 0, sizeof(mode));
	mode.hdisplay = width;
	mode.hsync_start = width + 48;
	mode.hsync_end = width + 80;
	mode.htotal = width + 160;
	mode.vdisplay = height;
	mode.vsync_start = height + 3;
	mode.vsync_end = height + 8;
	mode.vtotal = height + 31;
	mode.clock = (uint32)((uint64)mode.htotal * mode.vtotal * refresh / 1000);
	mode.vrefresh = refresh;
	mode.flags = DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_NVSYNC;
	mode.type = DRM_MODE_TYPE_DRIVER
		| (preferred ? DRM_MODE_TYPE_PREFERRED : 0);
	snprintf(mode.name, sizeof(mode.name), "%ux%u", width, height);
	return mode;
}


struct TestOutputs {
	std::vector<drmModeModeInfo> modes[4];
	KmsLayout layout;

	TestOutputs()
	{
		memset(&layout, 0, sizeof(layout));
		// a 1920x1080 monitor (60 and 50 Hz), a 2560x1440 one, a 1280x800
		// panel, and a disconnected connector
		modes[0].push_back(make_mode(1920, 1080, 60, true));
		modes[0].push_back(make_mode(1920, 1080, 50, false));
		modes[0].push_back(make_mode(1280, 720, 60, false));
		modes[1].push_back(make_mode(2560, 1440, 60, true));
		modes[1].push_back(make_mode(1920, 1080, 60, false));
		modes[2].push_back(make_mode(1280, 800, 60, true));
		layout.count = 4;
		for (uint32 i = 0; i < 4; i++) {
			KmsLayoutOutput& output = layout.outputs[i];
			output.id = 30 + i;
			output.connected = !modes[i].empty();
			output.modes = modes[i].empty() ? NULL : &modes[i][0];
			output.modeCount = modes[i].size();
			output.preferred = modes[i].empty() ? -1 : 0;
		}
	}
};


static display_output_config
make_config(uint32 id, int32 x, int32 y, uint16 scale, uint16 renderScale,
	const drmModeModeInfo* mode, bool mirror = false)
{
	display_output_config config;
	memset(&config, 0, sizeof(config));
	config.id = id;
	config.flags = B_DISPLAY_OUTPUT_ENABLED
		| (mirror ? B_DISPLAY_OUTPUT_MIRROR : 0);
	config.x = x;
	config.y = y;
	config.scale = scale;
	config.render_scale = renderScale;
	if (mode != NULL)
		kms_mode_to_timing(*mode, config.timing);
	return config;
}


// #pragma mark - modes


static void
test_modes()
{
	drmModeModeInfo mode = make_mode(1920, 1080, 60, true);
	display_timing timing;
	kms_mode_to_timing(mode, timing);
	CHECK_EQ(timing.h_display, 1920);
	CHECK_EQ(timing.v_total, 1080 + 31);
	CHECK_EQ(timing.pixel_clock, mode.clock);
	CHECK((timing.flags & B_POSITIVE_HSYNC) != 0);
	CHECK((timing.flags & B_POSITIVE_VSYNC) == 0);

	drmModeModeInfo back;
	kms_timing_to_mode(timing, back);
	CHECK_EQ(back.hdisplay, mode.hdisplay);
	CHECK_EQ(back.vsync_end, mode.vsync_end);
	CHECK_EQ(back.clock, mode.clock);
	CHECK((back.flags & DRM_MODE_FLAG_PHSYNC) != 0);
	CHECK((back.flags & DRM_MODE_FLAG_NVSYNC) != 0);
	CHECK_EQ(back.vrefresh, 59);	// the clock was rounded to kHz
	CHECK(strcmp(back.name, "1920x1080") == 0);

	CHECK_EQ(kms_refresh_millihertz(mode) / 1000, 59);
	bigtime_t period = kms_frame_period(mode);
	CHECK(period >= 16600 && period <= 16700);

	// the nearest refresh rate of the right size wins
	TestOutputs outputs;
	const KmsLayoutOutput& first = outputs.layout.outputs[0];
	drmModeModeInfo fifty = make_mode(1920, 1080, 50, false);
	kms_mode_to_timing(fifty, timing);
	CHECK_EQ(kms_find_mode(first.modes, first.modeCount, timing), 1);
	drmModeModeInfo sixty = make_mode(1920, 1080, 60, false);
	kms_mode_to_timing(sixty, timing);
	CHECK_EQ(kms_find_mode(first.modes, first.modeCount, timing), 0);
	drmModeModeInfo other = make_mode(1024, 768, 60, false);
	kms_mode_to_timing(other, timing);
	CHECK_EQ(kms_find_mode(first.modes, first.modeCount, timing), -1);
}


// #pragma mark - EDID


static std::vector<uint8>
make_edid()
{
	std::vector<uint8> edid(128, 0);
	const uint8 header[8] = { 0, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0 };
	memcpy(&edid[0], header, 8);
	// "VOS": 5 bit letters, 'A' = 1
	uint16 id = (('V' - '@') << 10) | (('O' - '@') << 5) | ('S' - '@');
	edid[8] = id >> 8;
	edid[9] = id & 0xff;
	edid[10] = 0x34;	// product 0x1234
	edid[11] = 0x12;
	edid[12] = 0x39;	// serial 12345
	edid[13] = 0x30;
	edid[16] = 12;		// week
	edid[17] = 2026 - 1990;
	edid[18] = 1;		// version 1.4
	edid[19] = 4;
	edid[20] = 0xa5;	// digital
	edid[21] = 53;		// 53 x 30 cm
	edid[22] = 30;

	// detailed timing: 1920x1080, 148.5 MHz, 527 x 296 mm
	uint8* d = &edid[54];
	uint16 clock = 14850;
	d[0] = clock & 0xff;
	d[1] = clock >> 8;
	d[2] = 1920 & 0xff;
	d[3] = 280 & 0xff;
	d[4] = ((1920 >> 8) << 4) | (280 >> 8);
	d[5] = 1080 & 0xff;
	d[6] = 45;
	d[7] = ((1080 >> 8) << 4) | 0;
	d[8] = 88;
	d[9] = 44;
	d[10] = (4 << 4) | 5;
	d[11] = 0;
	d[12] = 527 & 0xff;
	d[13] = 296 & 0xff;
	d[14] = ((527 >> 8) << 4) | (296 >> 8);
	d[17] = 0x18;

	// monitor name
	d = &edid[72];
	d[3] = 0xfc;
	memcpy(d + 5, "VOS Test Mon\n", 13);

	// range limits: 48-75 Hz, 30-83 kHz, 170 MHz
	d = &edid[90];
	d[3] = 0xfd;
	d[5] = 48;
	d[6] = 75;
	d[7] = 30;
	d[8] = 83;
	d[9] = 17;

	// serial string
	d = &edid[108];
	d[3] = 0xff;
	memcpy(d + 5, "SN-0042\n     ", 13);

	uint8 sum = 0;
	for (int i = 0; i < 127; i++)
		sum += edid[i];
	edid[127] = (uint8)(256 - sum);
	return edid;
}


static void
test_edid()
{
	std::vector<uint8> edid = make_edid();
	monitor_info info;
	CHECK_EQ(kms_edid_monitor_info(&edid[0], edid.size(), &info), B_OK);
	CHECK(strcmp(info.vendor, "VOS") == 0);
	CHECK(strcmp(info.name, "VOS Test Mon") == 0);
	CHECK(strcmp(info.serial_number, "SN-0042") == 0);
	CHECK_EQ(info.product_id, 0x1234);
	CHECK_EQ(info.produced.year, 2026);
	CHECK_EQ(info.produced.week, 12);
	CHECK(fabsf(info.width - 52.7f) < 0.01f);
	CHECK(fabsf(info.height - 29.6f) < 0.01f);
	CHECK_EQ(info.min_vertical_frequency, 48);
	CHECK_EQ(info.max_vertical_frequency, 75);
	CHECK_EQ(info.max_horizontal_frequency, 83);
	CHECK_EQ(info.max_pixel_clock, 170000);

	uint32 width = 0, height = 0;
	CHECK_EQ(kms_edid_preferred_size(&edid[0], edid.size(), &width, &height),
		B_OK);
	CHECK_EQ(width, 1920);
	CHECK_EQ(height, 1080);

	// too short, or no EDID header
	CHECK(kms_edid_monitor_info(&edid[0], 64, &info) != B_OK);
	edid[1] = 0;
	CHECK(kms_edid_monitor_info(&edid[0], edid.size(), &info) != B_OK);
}


// #pragma mark - layouts


static void
test_default_layout()
{
	TestOutputs outputs;
	kms_default_layout(outputs.layout);
	const KmsLayout& layout = outputs.layout;
	CHECK(layout.outputs[0].enabled);
	CHECK(layout.outputs[1].enabled);
	CHECK(layout.outputs[2].enabled);
	CHECK(!layout.outputs[3].enabled);
	CHECK_EQ(layout.outputs[1].x, 1920);
	CHECK_EQ(layout.outputs[2].x, 1920 + 2560);
	CHECK_EQ(layout.width, 1920 + 2560 + 1280);
	CHECK_EQ(layout.height, 1440);

	display_mode mode = kms_layout_mode(layout);
	CHECK_EQ(mode.virtual_width, layout.width);
	CHECK_EQ(mode.virtual_height, 1440);
	CHECK_EQ(mode.timing.h_display, layout.width);
	CHECK(kms_is_layout_mode(layout, mode));
	// the primary's refresh rate is kept (to a few mHz)
	drmModeModeInfo stretched;
	kms_timing_to_mode(mode.timing, stretched);
	CHECK(labs((long)kms_refresh_millihertz(stretched)
		- (long)kms_refresh_millihertz(outputs.modes[0][0])) < 100);

	display_mode list[64];
	uint32 count = kms_mode_list(NULL, NULL, outputs.layout, list, 64);
	CHECK_EQ(count, 1 + 3);	// the layout, the first monitor's three
	CHECK_EQ(list[0].virtual_width, layout.width);
	CHECK_EQ(list[1].virtual_width, 1920);
}


/*!	Two monitors side by side at every render scale: their regions meet
	without a gap or an overlap, each is its logical size times the render
	scale, and every logical pixel falls into exactly one of them. */
static void
test_layout_seams()
{
	for (uint32 s = 0; s < sizeof(kScales) / sizeof(kScales[0]); s++) {
		for (uint32 r = 0; r < sizeof(kScales) / sizeof(kScales[0]); r++) {
			uint16 scale = kScales[s];
			uint16 renderScale = kScales[r];
			TestOutputs outputs;
			const drmModeModeInfo& a = outputs.modes[0][0];
			const drmModeModeInfo& b = outputs.modes[1][0];
			int32 logicalA = (a.hdisplay * 100 + scale / 2) / scale;
			display_output_config configs[2] = {
				make_config(30, 0, 0, scale, renderScale, &a),
				make_config(31, logicalA, 0, scale, renderScale, &b)
			};
			CHECK_EQ(kms_build_layout(outputs.layout, configs, 2), B_OK);
			const KmsLayoutOutput& left = outputs.layout.outputs[0];
			const KmsLayoutOutput& right = outputs.layout.outputs[1];
			CHECK(left.enabled && right.enabled);
			CHECK_EQ(left.x, 0);
			// no seam: the right one starts where the left one ends. Drawn
			// one to one, a region is the panel's own size (as air/OS's
			// amdgpu_kms has it), and the rounded logical size can make it
			// overlap its neighbour by one column; never a gap.
			int32 overlap = left.x + (int32)left.width - right.x;
			if (scale == renderScale)
				CHECK(overlap >= 0 && overlap <= 1);
			else
				CHECK_EQ(overlap, 0);
			CHECK_EQ(right.x, kms_buffer_pixel(logicalA, renderScale));
			if (scale == renderScale) {
				CHECK_EQ(left.width, a.hdisplay);
				CHECK_EQ(right.height, b.vdisplay);
			} else {
				int32 logicalHeight = (b.vdisplay * 100 + scale / 2) / scale;
				CHECK_EQ(right.height,
					(uint32)kms_buffer_pixel(logicalHeight, renderScale));
			}
			CHECK_EQ(outputs.layout.width, (uint32)(right.x + right.width));

			// every logical column in exactly one region
			KmsRect regionA = kms_region(left);
			KmsRect regionB = kms_region(right);
			int32 total = logicalA
				+ (b.hdisplay * 100 + scale / 2) / scale;
			int32 misses = 0, doubles = 0;
			for (int32 x = 0; x < total; x++) {
				int32 bx = kms_buffer_pixel(x, renderScale);
				int32 in = (regionA.Contains(bx, 0) ? 1 : 0)
					+ (regionB.Contains(bx, 0) ? 1 : 0);
				if (in == 0)
					misses++;
				if (in > 1)
					doubles++;
			}
			CHECK_EQ(misses, 0);
			CHECK(doubles <= (overlap > 0 ? 1 : 0));
		}
	}
}


static void
test_layout_mirror()
{
	TestOutputs outputs;
	const drmModeModeInfo& a = outputs.modes[0][0];
	const drmModeModeInfo& c = outputs.modes[2][0];
	display_output_config configs[2] = {
		make_config(30, 0, 0, 100, 100, &a),
		make_config(32, 0, 0, 100, 100, &c, true)
	};
	CHECK_EQ(kms_build_layout(outputs.layout, configs, 2), B_OK);
	const KmsLayoutOutput& mirror = outputs.layout.outputs[2];
	CHECK(mirror.mirror);
	CHECK_EQ(mirror.width, 1920);
	CHECK_EQ(mirror.height, 1080);
	CHECK_EQ(outputs.layout.width, 1920);
	// 16:9 on a 16:10 panel: letterboxed, centered
	KmsRect destination = kms_destination(mirror);
	CHECK_EQ(destination.width, 1280);
	CHECK_EQ(destination.height, 720);
	CHECK_EQ(destination.y, 40);
	CHECK_EQ(destination.x, 0);

	// a mirror of nothing is a monitor of its own
	display_output_config lonely[2] = {
		make_config(30, 0, 0, 100, 100, &a),
		make_config(32, 5000, 0, 100, 100, &c, true)
	};
	CHECK_EQ(kms_build_layout(outputs.layout, lonely, 2), B_OK);
	CHECK(!outputs.layout.outputs[2].mirror);

	// the regions start at the origin
	display_output_config shifted[1] = {
		make_config(31, 300, 200, 100, 100, NULL)
	};
	CHECK_EQ(kms_build_layout(outputs.layout, shifted, 1), B_OK);
	CHECK_EQ(outputs.layout.outputs[1].x, 0);
	CHECK_EQ(outputs.layout.outputs[1].y, 0);
	CHECK(!outputs.layout.outputs[0].enabled);
	// no timing given: the preferred mode
	CHECK_EQ(outputs.layout.outputs[1].timing.hdisplay, 2560);

	// a disconnected or unknown output is refused
	display_output_config bad[1] = { make_config(33, 0, 0, 100, 100, NULL) };
	CHECK_EQ(kms_build_layout(outputs.layout, bad, 1), B_BAD_VALUE);
	display_output_config unknown[1] = { make_config(99, 0, 0, 100, 100, NULL) };
	CHECK_EQ(kms_build_layout(outputs.layout, unknown, 1), B_ENTRY_NOT_FOUND);
}


static void
test_single_mode_layout()
{
	TestOutputs outputs;
	display_mode mode;
	memset(&mode, 0, sizeof(mode));
	drmModeModeInfo small = make_mode(1280, 720, 60, false);
	kms_mode_to_timing(small, mode.timing);
	mode.virtual_width = 1280;
	mode.virtual_height = 720;
	CHECK_EQ(kms_single_mode_layout(outputs.layout, mode), B_OK);
	CHECK_EQ(outputs.layout.outputs[0].timing.hdisplay, 1280);
	CHECK(!outputs.layout.outputs[0].mirror);
	CHECK(outputs.layout.outputs[1].mirror);
	CHECK_EQ(outputs.layout.outputs[1].width, 1280);
	CHECK_EQ(outputs.layout.width, 1280);
}


// #pragma mark - render scale geometry


static void
test_damage_mapping()
{
	for (uint32 r = 0; r < sizeof(kScales) / sizeof(kScales[0]); r++) {
		uint16 renderScale = kScales[r];
		// a 1920x1080 monitor at 100 %, drawn at renderScale: the region
		// is larger and shrunk to the mode
		TestOutputs outputs;
		display_output_config config = make_config(30, 0, 0, 100,
			renderScale, &outputs.modes[0][0]);
		CHECK_EQ(kms_build_layout(outputs.layout, &config, 1), B_OK);
		const KmsLayoutOutput& output = outputs.layout.outputs[0];
		KmsRect region = kms_region(output);
		KmsRect destination = kms_destination(output);
		CHECK_EQ(destination.width, 1920);
		CHECK_EQ((int32)output.width,
			kms_buffer_pixel(1920, renderScale));

		// the whole region reaches the whole monitor
		KmsRect all = kms_map_damage(region, region, destination);
		CHECK(all == destination);

		// a one pixel change reaches the pixels whose filter covers it,
		// and those cover the pixel's own place
		for (int32 x = 0; x < (int32)output.width; x += 97) {
			KmsRect mapped = kms_map_damage(KmsRect(x, 7, 1, 1), region,
				destination);
			double place = (x + 0.5) * destination.width / region.width;
			CHECK(!mapped.IsEmpty());
			CHECK(mapped.x <= (int32)floor(place));
			CHECK(mapped.Right() > (int32)floor(place));
			CHECK(mapped.width <= 4);
		}
		// outside the region: nothing
		CHECK(kms_map_damage(KmsRect(region.Right(), 0, 10, 10), region,
			destination).IsEmpty());
	}
}


/*!	The cursor's hot spot lands on the monitor pixel showing the frame
	buffer pixel the pointer is on, for every render scale, and stays on
	the right monitor at the edge between two. */
static void
test_cursor_geometry()
{
	for (uint32 s = 0; s < sizeof(kScales) / sizeof(kScales[0]); s++) {
		for (uint32 r = 0; r < sizeof(kScales) / sizeof(kScales[0]); r++) {
			uint16 scale = kScales[s];
			uint16 renderScale = kScales[r];
			TestOutputs outputs;
			const drmModeModeInfo& a = outputs.modes[0][0];
			const drmModeModeInfo& b = outputs.modes[1][0];
			int32 logicalA = (a.hdisplay * 100 + scale / 2) / scale;
			display_output_config configs[2] = {
				make_config(30, 0, 0, scale, renderScale, &a),
				make_config(31, logicalA, 0, scale, renderScale, &b)
			};
			CHECK_EQ(kms_build_layout(outputs.layout, configs, 2), B_OK);
			const KmsLayoutOutput& left = outputs.layout.outputs[0];
			const KmsLayoutOutput& right = outputs.layout.outputs[1];

			// the hot spot at the cursor's density, 4 x 4 at 100 %
			int32 hot = (4 * renderScale) / 100;
			int32 errors = 0, wrong = 0;
			for (int32 lx = 0; lx < logicalA + 40; lx += 3) {
				int32 bx = kms_buffer_pixel(lx, renderScale);
				int32 by = kms_buffer_pixel(lx / 2, renderScale);
				const KmsLayoutOutput& on = bx < right.x ? left : right;
				if (!kms_region(on).Contains(bx, by))
					wrong++;
				int32 x, y, hotX, hotY;
				kms_cursor_position(on, bx, by, hot, hot, x, y, hotX, hotY);
				// where the hot spot is shown, back in buffer pixels
				double backX = on.x + (x + hotX + 0.5) * on.width
					/ on.timing.hdisplay;
				double backY = on.y + (y + hotY + 0.5) * on.height
					/ on.timing.vdisplay;
				double fx = (double)on.width / on.timing.hdisplay;
				if (fabs(backX - (bx + 0.5)) > fx / 2 + 0.51
					|| fabs(backY - (by + 0.5)) > fx / 2 + 0.51)
					errors++;
				// the hot spot is the scaled one
				CHECK(abs(hotX - (int32)floor(hot / fx + 0.5)) <= 1);
			}
			CHECK_EQ(wrong, 0);
			CHECK_EQ(errors, 0);
		}
	}
}


static void
test_scan_position()
{
	drmModeModeInfo mode = make_mode(1920, 1080, 60, true);
	bigtime_t period = kms_frame_period(mode);
	int32 line, lines;
	bigtime_t start, framePeriod;
	bigtime_t blank = 1000000;

	// at the start of the active scan
	kms_scan_position(mode, 1080, blank, blank, line, lines, start,
		framePeriod);
	CHECK_EQ(line, 0);
	CHECK_EQ(lines, 1080);
	CHECK_EQ(start, blank);
	CHECK_EQ(framePeriod, period);

	// half way down
	kms_scan_position(mode, 1080, blank + period * 1080 / mode.vtotal / 2,
		blank, line, lines, start, framePeriod);
	CHECK(abs(line - 540) <= 2);

	// in the blank: negative
	kms_scan_position(mode, 1080, blank + period - period / mode.vtotal * 5,
		blank, line, lines, start, framePeriod);
	CHECK(line < 0 && line >= -31);

	// three frames later: the frame's start moved along
	kms_scan_position(mode, 1080, blank + 3 * period + 10, blank, line,
		lines, start, framePeriod);
	CHECK_EQ(start, blank + 3 * period);
	CHECK(line >= 0 && line < 3);

	// a region drawn at 150 %: rows of the region
	kms_scan_position(mode, 1620, blank + period * 1080 / mode.vtotal / 2,
		blank, line, lines, start, framePeriod);
	CHECK_EQ(lines, 1620);
	CHECK(abs(line - 810) <= 3);
}


// #pragma mark - CPU resample


static uint32
pixel_at(const std::vector<uint32>& buffer, int32 width, int32 x, int32 y)
{
	return buffer[y * width + x];
}


static void
test_blit()
{
	// a copy keeps every pixel and makes it opaque
	std::vector<uint32> source(64 * 32), destination(64 * 32, 0);
	for (uint32 i = 0; i < source.size(); i++)
		source[i] = 0x00102030 + i;
	kms_copy_rect((const uint8*)&source[0], 64 * 4, (uint8*)&destination[0],
		64 * 4, KmsRect(5, 6, 10, 4), 5, 6, true);
	CHECK_EQ(pixel_at(destination, 64, 5, 6), 0xff000000 | source[6 * 64 + 5]);
	CHECK_EQ(pixel_at(destination, 64, 14, 9),
		0xff000000 | source[9 * 64 + 14]);
	CHECK_EQ(pixel_at(destination, 64, 15, 9), 0);
	CHECK_EQ(pixel_at(destination, 64, 4, 6), 0);

	// the same size: exactly the source
	KmsScaler identity;
	CHECK_EQ(identity.SetTo(64, 32, 64, 32), B_OK);
	CHECK(identity.IsIdentity());
	std::vector<uint32> copy(64 * 32, 0);
	identity.Scale((const uint8*)&source[0], 64 * 4, (uint8*)&copy[0],
		64 * 4, KmsRect(0, 0, 64, 32), false);
	CHECK(memcmp(&copy[0], &source[0], copy.size() * 4) == 0);

	for (uint32 r = 1; r < sizeof(kScales) / sizeof(kScales[0]); r++) {
		uint16 renderScale = kScales[r];
		// a region drawn at renderScale shrunk to a 100 % monitor
		int32 dw = 120, dh = 80;
		int32 sw = kms_buffer_pixel(dw, renderScale);
		int32 sh = kms_buffer_pixel(dh, renderScale);
		std::vector<uint32> region(sw * sh);

		// flat colour stays that colour, alpha opaque
		for (uint32 i = 0; i < region.size(); i++)
			region[i] = 0x00336699;
		KmsScaler scaler;
		CHECK_EQ(scaler.SetTo(sw, sh, dw, dh), B_OK);
		std::vector<uint32> out(dw * dh, 0);
		scaler.Scale((const uint8*)&region[0], sw * 4, (uint8*)&out[0],
			dw * 4, KmsRect(0, 0, dw, dh), true);
		int32 off = 0;
		for (uint32 i = 0; i < out.size(); i++) {
			if (out[i] != 0xff336699)
				off++;
		}
		CHECK_EQ(off, 0);

		// white stays white (the weights add up)
		for (uint32 i = 0; i < region.size(); i++)
			region[i] = 0xffffffff;
		scaler.Scale((const uint8*)&region[0], sw * 4, (uint8*)&out[0],
			dw * 4, KmsRect(0, 0, dw, dh), false);
		CHECK_EQ(out[dw * dh / 2], 0xffffffff);
		CHECK_EQ(out[0], 0xffffffff);
		CHECK_EQ(out[dw * dh - 1], 0xffffffff);

		// left half black, right half white: the edge stays where it was,
		// within the filter's reach
		for (int32 y = 0; y < sh; y++) {
			for (int32 x = 0; x < sw; x++)
				region[y * sw + x] = x < sw / 2 ? 0xff000000 : 0xffffffff;
		}
		scaler.Scale((const uint8*)&region[0], sw * 4, (uint8*)&out[0],
			dw * 4, KmsRect(0, 0, dw, dh), true);
		double edge = (sw / 2) * (double)dw / sw;
		int32 bad = 0;
		for (int32 y = 0; y < dh; y++) {
			for (int32 x = 0; x < dw; x++) {
				uint8 value = out[y * dw + x] & 0xff;
				if (x + 1 < edge - 1 && value != 0)
					bad++;
				if (x > edge + 1 && value != 255)
					bad++;
			}
		}
		CHECK_EQ(bad, 0);

		// a part computed alone is the same as computed with the rest
		std::vector<uint32> part(dw * dh, 0);
		KmsRect area(17, 9, 31, 23);
		scaler.Scale((const uint8*)&region[0], sw * 4, (uint8*)&part[0],
			dw * 4, area, true);
		int32 different = 0;
		for (int32 y = area.y; y < area.Bottom(); y++) {
			for (int32 x = area.x; x < area.Right(); x++) {
				if (part[y * dw + x] != out[y * dw + x])
					different++;
			}
		}
		CHECK_EQ(different, 0);
		CHECK_EQ(part[0], 0);

		// enlarging (a monitor at a higher scale than the drawing):
		// a 2x2 checkerboard of 1 px cells averages to grey in between
		KmsScaler enlarge;
		CHECK_EQ(enlarge.SetTo(dw, dh, sw, sh), B_OK);
		std::vector<uint32> small(dw * dh);
		for (int32 y = 0; y < dh; y++) {
			for (int32 x = 0; x < dw; x++)
				small[y * dw + x] = 0xff000000 | (x * 2) | ((y * 3) << 8);
		}
		std::vector<uint32> large(sw * sh, 0);
		enlarge.Scale((const uint8*)&small[0], dw * 4, (uint8*)&large[0],
			sw * 4, KmsRect(0, 0, sw, sh), true);
		// a linear ramp stays monotonic and within its range
		int32 notMonotonic = 0;
		for (int32 x = 1; x < sw; x++) {
			if ((large[sh / 2 * sw + x] & 0xff)
					< (large[sh / 2 * sw + x - 1] & 0xff))
				notMonotonic++;
		}
		CHECK_EQ(notMonotonic, 0);
		CHECK((large[sh / 2 * sw + sw - 1] & 0xff) <= (uint32)(dw - 1) * 2);
	}

	// damage mapping covers what the scaler changes: change one source
	// pixel, and every destination pixel that changed is inside the mapped
	// rectangle
	for (uint32 r = 1; r < sizeof(kScales) / sizeof(kScales[0]); r++) {
		int32 dw = 64, dh = 48;
		int32 sw = kms_buffer_pixel(dw, kScales[r]);
		int32 sh = kms_buffer_pixel(dh, kScales[r]);
		std::vector<uint32> region(sw * sh, 0xff000000);
		KmsScaler scaler;
		scaler.SetTo(sw, sh, dw, dh);
		std::vector<uint32> before(dw * dh), after(dw * dh);
		scaler.Scale((const uint8*)&region[0], sw * 4, (uint8*)&before[0],
			dw * 4, KmsRect(0, 0, dw, dh), true);
		int32 escaped = 0;
		for (int32 sx = 0; sx < sw; sx += 7) {
			int32 sy = (sx * 3) % sh;
			region[sy * sw + sx] = 0xffffffff;
			scaler.Scale((const uint8*)&region[0], sw * 4, (uint8*)&after[0],
				dw * 4, KmsRect(0, 0, dw, dh), true);
			KmsRect mapped = kms_map_damage(KmsRect(sx, sy, 1, 1),
				KmsRect(0, 0, sw, sh), KmsRect(0, 0, dw, dh));
			for (int32 y = 0; y < dh; y++) {
				for (int32 x = 0; x < dw; x++) {
					if (after[y * dw + x] != before[y * dw + x]
						&& !mapped.Contains(x, y))
						escaped++;
				}
			}
			region[sy * sw + sx] = 0xff000000;
		}
		CHECK_EQ(escaped, 0);
	}
}


int
main()
{
	test_modes();
	test_edid();
	test_default_layout();
	test_layout_seams();
	test_layout_mirror();
	test_single_mode_layout();
	test_damage_mapping();
	test_cursor_geometry();
	test_scan_position();
	test_blit();

	printf("kms_unit_test: %d checks, %d failed\n", sChecks, sFailures);
	return sFailures == 0 ? 0 : 1;
}
