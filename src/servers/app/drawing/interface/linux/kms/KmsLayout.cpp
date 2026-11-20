/*
 * Copyright 2026, Koan contributors.
 * Distributed under the terms of the MIT License.
 *
 * The layout rules follow air/OS's amdgpu_kms accelerant (layout.cpp,
 * mode.cpp; Copyright 2026, air/OS, MIT License), so that app_server sees
 * the same regions from this backend as from its multi-head accelerants.
 */


#include "KmsLayout.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>


#ifndef min_c
#	define min_c(a, b) ((a) > (b) ? (b) : (a))
#	define max_c(a, b) ((a) > (b) ? (a) : (b))
#endif


KmsRect
KmsRect::Intersect(const KmsRect& other) const
{
	int32 left = max_c(x, other.x);
	int32 top = max_c(y, other.y);
	int32 right = min_c(Right(), other.Right());
	int32 bottom = min_c(Bottom(), other.Bottom());
	if (right <= left || bottom <= top)
		return KmsRect();
	return KmsRect(left, top, right - left, bottom - top);
}


KmsRect
KmsRect::Union(const KmsRect& other) const
{
	if (IsEmpty())
		return other;
	if (other.IsEmpty())
		return *this;
	int32 left = min_c(x, other.x);
	int32 top = min_c(y, other.y);
	int32 right = max_c(Right(), other.Right());
	int32 bottom = max_c(Bottom(), other.Bottom());
	return KmsRect(left, top, right - left, bottom - top);
}


// #pragma mark - modes


void
kms_mode_to_timing(const drmModeModeInfo& mode, display_timing& timing)
{
	memset(&timing, 0, sizeof(timing));
	timing.pixel_clock = mode.clock;
	timing.h_display = mode.hdisplay;
	timing.h_sync_start = mode.hsync_start;
	timing.h_sync_end = mode.hsync_end;
	timing.h_total = mode.htotal;
	timing.v_display = mode.vdisplay;
	timing.v_sync_start = mode.vsync_start;
	timing.v_sync_end = mode.vsync_end;
	timing.v_total = mode.vtotal;
	if ((mode.flags & DRM_MODE_FLAG_PHSYNC) != 0)
		timing.flags |= B_POSITIVE_HSYNC;
	if ((mode.flags & DRM_MODE_FLAG_PVSYNC) != 0)
		timing.flags |= B_POSITIVE_VSYNC;
	if ((mode.flags & DRM_MODE_FLAG_INTERLACE) != 0)
		timing.flags |= B_TIMING_INTERLACED;
}


void
kms_timing_to_mode(const display_timing& timing, drmModeModeInfo& mode)
{
	memset(&mode, 0, sizeof(mode));
	mode.clock = timing.pixel_clock;
	mode.hdisplay = timing.h_display;
	mode.hsync_start = timing.h_sync_start;
	mode.hsync_end = timing.h_sync_end;
	mode.htotal = timing.h_total;
	mode.vdisplay = timing.v_display;
	mode.vsync_start = timing.v_sync_start;
	mode.vsync_end = timing.v_sync_end;
	mode.vtotal = timing.v_total;
	mode.flags = ((timing.flags & B_POSITIVE_HSYNC) != 0
			? DRM_MODE_FLAG_PHSYNC : DRM_MODE_FLAG_NHSYNC)
		| ((timing.flags & B_POSITIVE_VSYNC) != 0
			? DRM_MODE_FLAG_PVSYNC : DRM_MODE_FLAG_NVSYNC)
		| ((timing.flags & B_TIMING_INTERLACED) != 0
			? DRM_MODE_FLAG_INTERLACE : 0);
	if (mode.htotal != 0 && mode.vtotal != 0) {
		mode.vrefresh = (uint32)((uint64)mode.clock * 1000
			/ ((uint64)mode.htotal * mode.vtotal));
	}
	mode.type = DRM_MODE_TYPE_USERDEF;
	snprintf(mode.name, sizeof(mode.name), "%ux%u", mode.hdisplay,
		mode.vdisplay);
}


uint32
kms_refresh_millihertz(const drmModeModeInfo& mode)
{
	if (mode.htotal == 0 || mode.vtotal == 0)
		return 0;
	return (uint32)((uint64)mode.clock * 1000000
		/ ((uint64)mode.htotal * mode.vtotal));
}


/*!	The mode of \a timing's size whose refresh rate is nearest to its, or
	-1. app_server's timings may have no blanking, so pixel clocks are not
	compared. */
int32
kms_find_mode(const drmModeModeInfo* modes, uint32 count,
	const display_timing& timing)
{
	uint32 wanted = 0;
	if (timing.h_total != 0 && timing.v_total != 0) {
		wanted = (uint32)((uint64)timing.pixel_clock * 1000000
			/ ((uint64)timing.h_total * timing.v_total));
	}
	int32 best = -1;
	for (uint32 m = 0; m < count; m++) {
		const drmModeModeInfo& candidate = modes[m];
		if (candidate.hdisplay != timing.h_display
			|| candidate.vdisplay != timing.v_display
			|| (candidate.flags & DRM_MODE_FLAG_INTERLACE) != 0)
			continue;
		if (best < 0 || labs((long)kms_refresh_millihertz(candidate)
					- (long)wanted)
				< labs((long)kms_refresh_millihertz(modes[best])
					- (long)wanted))
			best = m;
	}
	return best;
}


/*!	A frame's duration in microseconds, 0 when the mode does not say. */
bigtime_t
kms_frame_period(const drmModeModeInfo& mode)
{
	if (mode.clock == 0 || mode.htotal == 0 || mode.vtotal == 0)
		return 0;
	bigtime_t period = (bigtime_t)mode.htotal * mode.vtotal * 1000
		/ mode.clock;
	if ((mode.flags & DRM_MODE_FLAG_INTERLACE) != 0)
		period /= 2;
	return period;
}


int32
kms_buffer_pixel(int32 logical, uint16 renderScale)
{
	int64 scaled = (int64)logical * renderScale;
	return (int32)(scaled >= 0 ? scaled / 100 : -((-scaled + 99) / 100));
}


// #pragma mark - layouts


static KmsLayoutOutput*
find_output(KmsLayout& layout, uint32 id)
{
	for (uint32 i = 0; i < layout.count; i++) {
		if (layout.outputs[i].id == id)
			return &layout.outputs[i];
	}
	return NULL;
}


static drmModeModeInfo
output_timing(const KmsLayoutOutput& output, const display_timing& timing)
{
	if (timing.h_display == 0)
		return output.modes[output.preferred >= 0 ? output.preferred : 0];
	int32 best = kms_find_mode(output.modes, output.modeCount, timing);
	if (best >= 0)
		return output.modes[best];
	drmModeModeInfo mode;
	kms_timing_to_mode(timing, mode);
	return mode;
}


static void
finish_layout(KmsLayout& layout)
{
	layout.width = layout.height = 0;
	for (uint32 i = 0; i < layout.count; i++) {
		const KmsLayoutOutput& output = layout.outputs[i];
		if (!output.enabled || output.mirror)
			continue;
		layout.width = max_c(layout.width, (uint32)(output.x + output.width));
		layout.height = max_c(layout.height,
			(uint32)(output.y + output.height));
	}
}


/*!	Each enabled monitor shows the region where app_server draws it: its
	logical size (its timing at its scale) times the render scale, from
	the buffer pixel of its first logical pixel to that of the one after its
	last, so that monitors side by side share their edge and nothing is
	left between them at fractional scales. \a layout comes with the
	outputs' connectors and modes; the regions start at the origin.
*/
status_t
kms_build_layout(KmsLayout& layout, const display_output_config* configs,
	uint32 count)
{
	for (uint32 i = 0; i < layout.count; i++) {
		layout.outputs[i].enabled = false;
		layout.outputs[i].mirror = false;
	}

	bool any = false;
	for (uint32 i = 0; i < count; i++) {
		const display_output_config& config = configs[i];
		KmsLayoutOutput* output = find_output(layout, config.id);
		if (output == NULL)
			return B_ENTRY_NOT_FOUND;
		if ((config.flags & B_DISPLAY_OUTPUT_ENABLED) == 0)
			continue;
		if (!output->connected || output->modeCount == 0)
			return B_BAD_VALUE;
		output->enabled = true;
		output->mirror = (config.flags & B_DISPLAY_OUTPUT_MIRROR) != 0;
		output->timing = output_timing(*output, config.timing);
		output->scale = config.scale != 0 ? config.scale : 100;
		output->renderScale = config.render_scale != 0
			? config.render_scale : 100;
		uint32 logicalWidth = (output->timing.hdisplay * 100
			+ output->scale / 2) / output->scale;
		uint32 logicalHeight = (output->timing.vdisplay * 100
			+ output->scale / 2) / output->scale;
		output->x = kms_buffer_pixel(config.x, output->renderScale);
		output->y = kms_buffer_pixel(config.y, output->renderScale);
		if (output->scale == output->renderScale) {
			// one to one: the monitor's own pixels (a rounded logical
			// size would stretch the region by a pixel over the panel)
			output->width = output->timing.hdisplay;
			output->height = output->timing.vdisplay;
		} else {
			output->width = kms_buffer_pixel(config.x + logicalWidth,
				output->renderScale) - output->x;
			output->height = kms_buffer_pixel(config.y + logicalHeight,
				output->renderScale) - output->y;
		}
		any = true;
	}
	if (!any)
		return B_BAD_VALUE;

	int32 left = INT32_MAX, top = INT32_MAX;
	for (uint32 i = 0; i < layout.count; i++) {
		const KmsLayoutOutput& output = layout.outputs[i];
		if (output.enabled && !output.mirror) {
			left = min_c(left, output.x);
			top = min_c(top, output.y);
		}
	}
	if (left == INT32_MAX) {
		// only mirrors: the first is a monitor of its own
		for (uint32 i = 0; i < layout.count; i++) {
			if (layout.outputs[i].enabled) {
				layout.outputs[i].mirror = false;
				left = layout.outputs[i].x;
				top = layout.outputs[i].y;
				break;
			}
		}
	}
	for (uint32 i = 0; i < layout.count; i++) {
		KmsLayoutOutput& output = layout.outputs[i];
		if (!output.enabled)
			continue;
		output.x -= left;
		output.y -= top;
	}
	// a mirror shows the region of the output it mirrors
	for (uint32 i = 0; i < layout.count; i++) {
		KmsLayoutOutput& output = layout.outputs[i];
		if (!output.enabled || !output.mirror)
			continue;
		const KmsLayoutOutput* source = NULL;
		for (uint32 j = 0; j < layout.count; j++) {
			const KmsLayoutOutput& other = layout.outputs[j];
			if (other.enabled && !other.mirror && other.x == output.x
				&& other.y == output.y)
				source = &other;
		}
		if (source == NULL) {
			output.mirror = false;
			continue;
		}
		output.width = source->width;
		output.height = source->height;
	}
	finish_layout(layout);
	return B_OK;
}


/*!	Every connected monitor at its preferred mode, side by side, as its
	connectors are numbered. */
void
kms_default_layout(KmsLayout& layout)
{
	int32 x = 0;
	for (uint32 i = 0; i < layout.count; i++) {
		KmsLayoutOutput& output = layout.outputs[i];
		output.enabled = output.connected && output.preferred >= 0;
		output.mirror = false;
		if (!output.enabled)
			continue;
		output.timing = output.modes[output.preferred];
		output.scale = output.renderScale = 100;
		output.x = x;
		output.y = 0;
		output.width = output.timing.hdisplay;
		output.height = output.timing.vdisplay;
		x += output.width;
	}
	finish_layout(layout);
}


/*!	A layout for a mode that is no layout's: the first monitor at that
	size, the others mirroring it at their own preferred modes. */
status_t
kms_single_mode_layout(KmsLayout& layout, const display_mode& mode)
{
	kms_default_layout(layout);
	bool primaryDone = false;
	for (uint32 i = 0; i < layout.count; i++) {
		KmsLayoutOutput& output = layout.outputs[i];
		if (!output.enabled)
			continue;
		output.x = output.y = 0;
		output.width = mode.virtual_width;
		output.height = mode.virtual_height;
		if (!primaryDone) {
			int32 best = kms_find_mode(output.modes, output.modeCount,
				mode.timing);
			if (best >= 0)
				output.timing = output.modes[best];
			else if (mode.timing.h_display != 0)
				kms_timing_to_mode(mode.timing, output.timing);
			output.mirror = false;
			primaryDone = true;
		} else
			output.mirror = true;
	}
	layout.width = mode.virtual_width;
	layout.height = mode.virtual_height;
	return primaryDone ? B_OK : B_ENTRY_NOT_FOUND;
}


const KmsLayoutOutput*
kms_layout_primary(const KmsLayout& layout)
{
	const KmsLayoutOutput* primary = NULL;
	for (uint32 i = 0; i < layout.count; i++) {
		const KmsLayoutOutput& output = layout.outputs[i];
		if (!output.enabled || output.mirror)
			continue;
		if (primary == NULL || output.y < primary->y
			|| (output.y == primary->y && output.x < primary->x))
			primary = &output;
	}
	return primary;
}


/*!	The mode app_server sees for a layout: the frame buffer, with the
	primary monitor's timing stretched to its size at the same refresh. */
display_mode
kms_layout_mode(const KmsLayout& layout)
{
	display_mode mode;
	memset(&mode, 0, sizeof(mode));
	mode.space = B_RGB32;
	mode.virtual_width = layout.width;
	mode.virtual_height = layout.height;
	mode.flags = B_HARDWARE_CURSOR | B_PARALLEL_ACCESS | B_DPMS;
	const KmsLayoutOutput* primary = kms_layout_primary(layout);
	if (primary == NULL)
		return mode;
	drmModeModeInfo timing = primary->timing;
	uint32 refresh = kms_refresh_millihertz(timing);
	uint32 hBlank = timing.htotal - timing.hdisplay;
	uint32 vBlank = timing.vtotal - timing.vdisplay;
	int32 hSync = timing.hsync_start - timing.hdisplay;
	int32 vSync = timing.vsync_start - timing.vdisplay;
	timing.hsync_start = layout.width + hSync;
	timing.hsync_end = timing.hsync_start + (timing.hsync_end
		- primary->timing.hsync_start);
	timing.htotal = layout.width + hBlank;
	timing.hdisplay = layout.width;
	timing.vsync_start = layout.height + vSync;
	timing.vsync_end = timing.vsync_start + (timing.vsync_end
		- primary->timing.vsync_start);
	timing.vtotal = layout.height + vBlank;
	timing.vdisplay = layout.height;
	if (refresh != 0) {
		timing.clock = (uint32)((uint64)timing.htotal * timing.vtotal
			* refresh / 1000000);
	}
	kms_mode_to_timing(timing, mode.timing);
	return mode;
}


bool
kms_is_layout_mode(const KmsLayout& layout, const display_mode& mode)
{
	return layout.width != 0 && mode.virtual_width == layout.width
		&& mode.virtual_height == layout.height
		&& mode.timing.h_display == layout.width
		&& mode.timing.v_display == layout.height;
}


static void
add_mode(display_mode* list, uint32& count, uint32 max,
	const display_mode& mode)
{
	if (mode.virtual_width == 0 || mode.virtual_height == 0)
		return;
	for (uint32 i = 0; i < count; i++) {
		if (list[i].virtual_width == mode.virtual_width
			&& list[i].virtual_height == mode.virtual_height
			&& list[i].timing.pixel_clock == mode.timing.pixel_clock)
			return;
	}
	if (count < max)
		list[count++] = mode;
}


/*!	The modes app_server is offered: the pending layout, the current one
	and the default first, then the first monitor's own modes. */
uint32
kms_mode_list(const KmsLayout* pending, const display_mode* current,
	const KmsLayout& outputs, display_mode* list, uint32 max)
{
	uint32 count = 0;
	if (pending != NULL)
		add_mode(list, count, max, kms_layout_mode(*pending));
	if (current != NULL)
		add_mode(list, count, max, *current);
	KmsLayout* layout = (KmsLayout*)malloc(sizeof(KmsLayout));
	if (layout != NULL) {
		*layout = outputs;
		kms_default_layout(*layout);
		add_mode(list, count, max, kms_layout_mode(*layout));
		free(layout);
	}
	for (uint32 i = 0; i < outputs.count; i++) {
		const KmsLayoutOutput& output = outputs.outputs[i];
		if (!output.connected)
			continue;
		for (uint32 m = 0; m < output.modeCount; m++) {
			if ((output.modes[m].flags & DRM_MODE_FLAG_INTERLACE) != 0)
				continue;
			display_mode mode;
			memset(&mode, 0, sizeof(mode));
			kms_mode_to_timing(output.modes[m], mode.timing);
			mode.space = B_RGB32;
			mode.virtual_width = output.modes[m].hdisplay;
			mode.virtual_height = output.modes[m].vdisplay;
			mode.flags = B_HARDWARE_CURSOR | B_PARALLEL_ACCESS | B_DPMS;
			add_mode(list, count, max, mode);
		}
		break;
	}
	return count;
}


// #pragma mark - geometry


KmsRect
kms_region(const KmsLayoutOutput& output)
{
	return KmsRect(output.x, output.y, output.width, output.height);
}


KmsRect
kms_destination(const KmsLayoutOutput& output)
{
	KmsRect destination(0, 0, output.timing.hdisplay, output.timing.vdisplay);
	if (!output.mirror || output.width == 0 || output.height == 0)
		return destination;
	uint64 scaledWidth = (uint64)output.width * destination.height
		/ output.height;
	if (scaledWidth <= (uint64)destination.width) {
		destination.x = (destination.width - scaledWidth) / 2;
		destination.width = scaledWidth;
	} else {
		uint64 scaledHeight = (uint64)output.height * destination.width
			/ output.width;
		destination.y = (destination.height - scaledHeight) / 2;
		destination.height = scaledHeight;
	}
	return destination;
}


/*!	The destination pixels [first, last] that source pixels [from, to]
	(region-relative) reach when \a sourceLength pixels are filtered to
	\a destinationLength, with KmsScaler's filter (a tent of radius
	max(1, source / destination) source pixels).
*/
static void
map_span(int32 from, int32 to, int32 sourceLength, int32 destinationLength,
	int32& first, int32& last)
{
	if (sourceLength == destinationLength) {
		first = from;
		last = to;
		return;
	}
	double ratio = (double)sourceLength / destinationLength;
	double support = ratio > 1.0 ? ratio : 1.0;
	// destination pixel i samples around (i + 0.5) * ratio - 0.5
	first = (int32)floor((from - support + 0.5) / ratio - 0.5);
	last = (int32)ceil((to + support + 0.5) / ratio - 0.5);
	if (first < 0)
		first = 0;
	if (last > destinationLength - 1)
		last = destinationLength - 1;
}


KmsRect
kms_map_damage(const KmsRect& area, const KmsRect& region,
	const KmsRect& destination)
{
	KmsRect inside = area.Intersect(region);
	if (inside.IsEmpty() || destination.IsEmpty())
		return KmsRect();
	int32 left, right, top, bottom;
	map_span(inside.x - region.x, inside.Right() - 1 - region.x, region.width,
		destination.width, left, right);
	map_span(inside.y - region.y, inside.Bottom() - 1 - region.y,
		region.height, destination.height, top, bottom);
	return KmsRect(destination.x + left, destination.y + top,
		right - left + 1, bottom - top + 1);
}


void
kms_cursor_position(const KmsLayoutOutput& output, int32 bufferX,
	int32 bufferY, int32 hotX, int32 hotY, int32& x, int32& y,
	int32& scaledHotX, int32& scaledHotY)
{
	KmsRect destination = kms_destination(output);
	if (output.width == 0 || output.height == 0) {
		x = bufferX - output.x - hotX;
		y = bufferY - output.y - hotY;
		scaledHotX = hotX;
		scaledHotY = hotY;
		return;
	}
	// the pointer's buffer pixel, its center, in the monitor's pixels
	double fx = (double)destination.width / output.width;
	double fy = (double)destination.height / output.height;
	int32 px = destination.x
		+ (int32)floor((bufferX - output.x + 0.5) * fx);
	int32 py = destination.y
		+ (int32)floor((bufferY - output.y + 0.5) * fy);
	scaledHotX = (int32)floor(hotX * fx + 0.5);
	scaledHotY = (int32)floor(hotY * fy + 0.5);
	x = px - scaledHotX;
	y = py - scaledHotY;
}


void
kms_scan_position(const drmModeModeInfo& mode, uint32 regionHeight,
	bigtime_t now, bigtime_t lastBlank, int32& line, int32& lines,
	bigtime_t& scanStart, bigtime_t& period)
{
	period = kms_frame_period(mode);
	lines = regionHeight != 0 ? regionHeight : mode.vdisplay;
	if (period <= 0 || mode.vtotal == 0 || lastBlank <= 0) {
		line = 0;
		scanStart = 0;
		return;
	}
	bigtime_t since = now - lastBlank;
	if (since < 0)
		since = 0;
	bigtime_t frames = since / period;
	scanStart = lastBlank + frames * period;
	bigtime_t inFrame = since - frames * period;
	// rows of the mode since the active scan began
	int64 row = (int64)inFrame * mode.vtotal / period;
	if (row >= mode.vdisplay) {
		// in the blank: minus the rows until the scan starts again
		row -= mode.vtotal;
	}
	line = (int32)(row * (int64)lines / mode.vdisplay);
}
