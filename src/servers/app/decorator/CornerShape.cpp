/*
 * Copyright 2026, Koan contributors. All rights reserved.
 * Distributed under the terms of the MIT License.
 */


#include "CornerShape.h"

#include <math.h>


// Radii above this are clamped; window corners are a few units round.
static const int32_t kMaxRadius = 64;


/*!	The device pixels covered by logical cell \a cell at \a scale, the way
	the painter scales clipping rectangles (edges rounded down).
*/
static inline void
device_span(int32_t cell, float scale, int32_t& first, int32_t& last)
{
	first = (int32_t)floorf(cell * scale);
	last = (int32_t)floorf((cell + 1) * scale) - 1;
}


int32_t
CornerShape::ClampRadius(float radius, int32_t width, int32_t height)
{
	if (!(radius > 0) || width <= 0 || height <= 0)
		return 0;

	int32_t result = (int32_t)floorf(radius);
	if (result > kMaxRadius)
		result = kMaxRadius;
	if (result > width / 2)
		result = width / 2;
	if (result > height / 2)
		result = height / 2;
	return result > 0 ? result : 0;
}


int32_t
CornerShape::CutWidths(int32_t radius, float scale, int32_t* widths,
	int32_t maxRows)
{
	if (widths == NULL || radius <= 0 || maxRows <= 0)
		return 0;
	if (radius > kMaxRadius)
		radius = kMaxRadius;
	if (!(scale >= 1.0f))
		scale = 1.0f;

	// The quarter circle's center, in device pixels from the corner.
	const double center = radius * (double)scale;
	const double radius2 = center * center;

	int32_t rows = 0;
	for (int32_t y = 0; y < radius && y < maxRows; y++) {
		int32_t firstY, lastY;
		device_span(y, scale, firstY, lastY);

		int32_t width = 0;
		for (int32_t x = 0; x < radius; x++) {
			int32_t firstX, lastX;
			device_span(x, scale, firstX, lastX);

			// The cell's point nearest to the center, the inner corner of its
			// innermost pixel, decides: if even that lies outside the
			// circle, all of the cell does, and no pixel the curve touches
			// (where the decorator draws the anti-aliased edge) is cut.
			double dx = center - (lastX + 1);
			double dy = center - (lastY + 1);
			if (dx * dx + dy * dy <= radius2)
				break;
			width = x + 1;
		}

		widths[y] = width;
		if (width > 0)
			rows = y + 1;
	}

	for (int32_t y = radius; y < maxRows; y++)
		widths[y] = 0;

	return rows;
}


int32_t
CornerShape::CutRects(int32_t left, int32_t top, int32_t right,
	int32_t bottom, float radius, float scale, CornerRect* rects,
	int32_t maxRects)
{
	if (rects == NULL || maxRects <= 0 || right < left || bottom < top)
		return 0;

	int32_t clamped = ClampRadius(radius, right - left + 1,
		bottom - top + 1);
	if (clamped <= 0)
		return 0;

	int32_t widths[kMaxRadius];
	int32_t rows = CutWidths(clamped, scale, widths, kMaxRadius);

	int32_t count = 0;
	for (int32_t y = 0; y < rows; y++) {
		int32_t width = widths[y];
		if (width <= 0)
			continue;
		if (count + 4 > maxRects)
			break;

		CornerRect topLeft = { left, top + y, left + width - 1, top + y };
		CornerRect topRight = { right - width + 1, top + y, right, top + y };
		CornerRect bottomLeft = { left, bottom - y, left + width - 1,
			bottom - y };
		CornerRect bottomRight = { right - width + 1, bottom - y, right,
			bottom - y };
		rects[count++] = topLeft;
		rects[count++] = topRight;
		rects[count++] = bottomLeft;
		rects[count++] = bottomRight;
	}

	return count;
}
