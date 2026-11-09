/*
 * Copyright 2026, Koan contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef SCREEN_SCALE_TRANSFORM_H
#define SCREEN_SCALE_TRANSFORM_H

#include <Point.h>
#include <Rect.h>

#include <math.h>


namespace ScreenScaleTransform {

inline float
Normalize(float scale)
{
	if (!(scale > 0.0f))
		return 1.0f;
	if (scale > 4.0f)
		return 4.0f;
	return scale;
}


inline BRect
ToLogicalFrame(const BRect& physicalFrame, float scale)
{
	scale = Normalize(scale);
	if (scale == 1.0f)
		return physicalFrame;

	return BRect(physicalFrame.left / scale, physicalFrame.top / scale,
		(physicalFrame.right + 1.0f) / scale - 1.0f,
		(physicalFrame.bottom + 1.0f) / scale - 1.0f);
}


inline BRect
ToPhysicalRect(const BRect& logicalRect, float scale)
{
	scale = Normalize(scale);
	if (scale == 1.0f)
		return logicalRect;

	return BRect(floorf(logicalRect.left * scale),
		floorf(logicalRect.top * scale),
		ceilf((logicalRect.right + 1.0f) * scale) - 1.0f,
		ceilf((logicalRect.bottom + 1.0f) * scale) - 1.0f);
}


/*!	Framebuffer column/row at which the logical pixel boundary \a logicalEdge
	starts. Pixel px belongs to logical pixel c iff
	SnapEdge(c) <= px < SnapEdge(c + 1), i.e. c <= px / scale < c + 1: the
	same answer MouseFilter gives (ToLogical() then BRegion's int
	truncation). Every boundary goes through this one function, so
	adjacent logical rects never share or skip a physical pixel.
*/
inline int32
SnapEdge(float logicalEdge, float scale)
{
	return (int32)ceil((double)logicalEdge * Normalize(scale));
}


/*!	Framebuffer rect that a logical rect owns for clipping, filling and
	damage. Adjacent logical rects map to adjacent physical rects (a
	partition), and a physical pixel belongs to the rect that pointer
	hit-testing assigns it to. Unlike ToPhysicalRect() it does not expand,
	so it may come out empty (invalid) when scale < 1.
*/
inline BRect
ToPhysicalPartitionRect(const BRect& logicalRect, float scale)
{
	scale = Normalize(scale);
	if (scale == 1.0f)
		return logicalRect;

	return BRect(SnapEdge(logicalRect.left, scale),
		SnapEdge(logicalRect.top, scale),
		SnapEdge(logicalRect.right + 1.0f, scale) - 1,
		SnapEdge(logicalRect.bottom + 1.0f, scale) - 1);
}


/*!	Converts a logical blit offset to framebuffer pixels. Returns false when
	no whole-pixel copy reproduces what a repaint at the new position would
	draw (offset * scale is not an integer); the caller must then repaint
	instead of copying. When it returns true, SnapEdge(e + offset)
	== SnapEdge(e) + physicalOffset for every edge e.
*/
inline bool
ToPhysicalOffset(int32 logicalOffset, float scale, int32& physicalOffset)
{
	double exact = (double)logicalOffset * Normalize(scale);
	physicalOffset = (int32)round(exact);
	return exact == (double)physicalOffset;
}


/*!	Framebuffer rect the pointing devices must clamp to so the pointer stays
	inside the logical rect \a confineTo (BWindow::AcquirePointerGrab).
	input_server clamps in framebuffer pixels (Desktop passes it the
	physical screen bounds).
*/
inline BRect
ToDeviceConfineRect(const BRect& confineTo, const BRect& physicalFrame,
	const BRect& logicalFrame, float scale)
{
	if (Normalize(scale) == 1.0f && physicalFrame == logicalFrame)
		return confineTo;

	BRect relative = confineTo.OffsetByCopy(-logicalFrame.left,
		-logicalFrame.top);
	return ToPhysicalPartitionRect(relative, scale).OffsetByCopy(
		physicalFrame.left, physicalFrame.top);
}


inline BPoint
ToLogical(const BPoint& physical, const BRect& physicalFrame,
	const BRect& logicalFrame, float scale)
{
	scale = Normalize(scale);
	return BPoint(logicalFrame.left
			+ (physical.x - physicalFrame.left) / scale,
		logicalFrame.top + (physical.y - physicalFrame.top) / scale);
}


inline BPoint
ToPhysical(const BPoint& logical, const BRect& physicalFrame,
	const BRect& logicalFrame, float scale)
{
	scale = Normalize(scale);
	return BPoint(physicalFrame.left
			+ (logical.x - logicalFrame.left) * scale,
		physicalFrame.top + (logical.y - logicalFrame.top) * scale);
}

}	// namespace ScreenScaleTransform

#endif	// SCREEN_SCALE_TRANSFORM_H
