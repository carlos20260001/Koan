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
