/*
 * Copyright 2026, Koan contributors. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef KMS_LAYOUT_H
#define KMS_LAYOUT_H


/*	The display layout of a KMS device, as air/OS's multi-head accelerants
	keep it (amdgpu_kms's layout.cpp and mode.cpp are the model): every
	monitor shows a region of one frame buffer, the region being the
	monitor's logical size times the render scale, placed where app_server
	draws the monitor's first logical pixel. Pure functions on plain data,
	no DRM file descriptor: the unit tests use them as they are.

	Coordinates: "logical" pixels are app_server's and input_server's;
	"frame buffer" (buffer) pixels are the render scale times those
	(floor(logical * scale / 100) is where a logical pixel starts); "mode"
	pixels are a monitor's own. A region is scaled to its monitor's mode
	when the two sizes differ (by the display engine, the GPU or the CPU).
*/


#include <Accelerant.h>
#include <SupportDefs.h>

#include <xf86drmMode.h>


static const uint32 kKmsMaxOutputs = 16;


struct KmsRect {
	int32	x;
	int32	y;
	int32	width;
	int32	height;

			KmsRect() : x(0), y(0), width(0), height(0) {}
			KmsRect(int32 _x, int32 _y, int32 _width, int32 _height)
				: x(_x), y(_y), width(_width), height(_height) {}

	bool	IsEmpty() const { return width <= 0 || height <= 0; }
	int32	Right() const { return x + width; }		// exclusive
	int32	Bottom() const { return y + height; }	// exclusive
	KmsRect	Intersect(const KmsRect& other) const;
	KmsRect	Union(const KmsRect& other) const;
	bool	Contains(int32 px, int32 py) const
				{ return px >= x && py >= y && px < Right() && py < Bottom(); }
	bool	operator==(const KmsRect& other) const
				{ return x == other.x && y == other.y && width == other.width
					&& height == other.height; }
};


/*!	One connector as the layout sees it. \c modes points into the device's
	own copy of the connector's modes. */
struct KmsLayoutOutput {
	uint32					id;				// the DRM connector
	bool					connected;
	const drmModeModeInfo*	modes;
	uint32					modeCount;
	int32					preferred;		// index into modes, -1: none

	// how it is driven
	bool					enabled;
	bool					mirror;
	int32					x;				// region in frame buffer pixels
	int32					y;
	uint32					width;
	uint32					height;
	uint16					scale;
	uint16					renderScale;
	drmModeModeInfo			timing;			// the mode on the wire
};


struct KmsLayout {
	KmsLayoutOutput			outputs[kKmsMaxOutputs];
	uint32					count;
	uint32					width;			// the frame buffer
	uint32					height;
};


// modes and timings
void		kms_mode_to_timing(const drmModeModeInfo& mode,
				display_timing& timing);
void		kms_timing_to_mode(const display_timing& timing,
				drmModeModeInfo& mode);
uint32		kms_refresh_millihertz(const drmModeModeInfo& mode);
int32		kms_find_mode(const drmModeModeInfo* modes, uint32 count,
				const display_timing& timing);
bigtime_t	kms_frame_period(const drmModeModeInfo& mode);

// the first frame buffer pixel of logical pixel \a logical
int32		kms_buffer_pixel(int32 logical, uint16 renderScale);

// app_server's display_output_config list into regions
status_t	kms_build_layout(KmsLayout& layout,
				const display_output_config* configs, uint32 count);
void		kms_default_layout(KmsLayout& layout);
status_t	kms_single_mode_layout(KmsLayout& layout,
				const display_mode& mode);
const KmsLayoutOutput* kms_layout_primary(const KmsLayout& layout);
display_mode kms_layout_mode(const KmsLayout& layout);
bool		kms_is_layout_mode(const KmsLayout& layout,
				const display_mode& mode);
uint32		kms_mode_list(const KmsLayout* pending, const display_mode* current,
				const KmsLayout& outputs, display_mode* list, uint32 max);

// where an output shows its region on its monitor (a mirror keeps the
// region's shape, centered)
KmsRect		kms_destination(const KmsLayoutOutput& output);
KmsRect		kms_region(const KmsLayoutOutput& output);

// frame buffer -> monitor: the mode pixels a change in \a area (frame
// buffer pixels) can reach on \a destination when \a region is filtered
// to it (KmsScaler's filter)
KmsRect		kms_map_damage(const KmsRect& area, const KmsRect& region,
				const KmsRect& destination);

// where the cursor plane goes: \a bufferX/Y is the pointer in frame buffer
// pixels, \a hotX/Y the hot spot at the frame buffer's density; the result
// is the top left corner in the monitor's pixels, and the hot spot scaled
void		kms_cursor_position(const KmsLayoutOutput& output, int32 bufferX,
				int32 bufferY, int32 hotX, int32 hotY, int32& x, int32& y,
				int32& scaledHotX, int32& scaledHotY);

// where the scan is: \a now and \a lastBlank (the start of the active scan
// of the last frame, as DRM time stamps it) in microseconds; the row of the
// region (negative in the blank) and the region's rows
void		kms_scan_position(const drmModeModeInfo& mode, uint32 regionHeight,
				bigtime_t now, bigtime_t lastBlank, int32& line, int32& lines,
				bigtime_t& scanStart, bigtime_t& period);


#endif	// KMS_LAYOUT_H
