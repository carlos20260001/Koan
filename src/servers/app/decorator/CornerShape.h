/*
 * Copyright 2026, Koan contributors. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef _CORNER_SHAPE_H
#define _CORNER_SHAPE_H


#include <stdint.h>


/*!	Rounded window corners as rectangles, for decorators whose windows have
	a rounded outline.

	A window's shape is a region in logical coordinates, made of whole
	logical units (clipping_rect); the painter scales it to device pixels at
	the render scale, rounding every edge down. The cut-away corners are
	therefore whole logical cells, chosen at device resolution: a cell is
	cut only when every device pixel it covers lies entirely outside the
	corner's quarter circle. What is left of the curve
	inside the kept cells is drawn by the decorator with an anti-aliased
	outline, which makes the edge look smooth at any scale.

	The code is plain C++ so a host test can cover it without the window
	server; decorators turn the rectangles into a BRegion.
*/


struct CornerRect {
	int32_t				left;
	int32_t				top;
	int32_t				right;
	int32_t				bottom;
		// inclusive, like clipping_rect
};


class CornerShape {
public:
	/*!	The largest usable radius for a window of the given outer size (in
		logical units): at most half of each side, in whole units, and never
		negative.
	*/
	static	int32_t				ClampRadius(float radius, int32_t width,
									int32_t height);

	/*!	Fills \a widths with the number of logical cells to cut in each of
		the first \a radius rows of a top left corner (row 0 is the outer
		edge), at the render \a scale (1.0, 1.25, 1.5, 2.0, ...). The widths
		never grow from one row to the next, and the cut cells of row y and
		of column y mirror each other.

		\return the number of rows with a cut, at most \a maxRows.
	*/
	static	int32_t				CutWidths(int32_t radius, float scale,
									int32_t* widths, int32_t maxRows);

	/*!	The rectangles to cut from the four corners of the outer bounds
		[\a left, \a right] x [\a top, \a bottom] (inclusive, logical) for
		corners of \a radius at \a scale. Corners are mirrored copies of the
		top left one. \a radius is clamped with ClampRadius().

		\param rects receives up to \a maxRects rectangles.
		\return the number of rectangles written.
	*/
	static	int32_t				CutRects(int32_t left, int32_t top,
									int32_t right, int32_t bottom,
									float radius, float scale,
									CornerRect* rects, int32_t maxRects);

	//! The most rectangles CutRects() writes for \a radius.
	static	int32_t				MaxRects(int32_t radius)
									{ return radius > 0 ? 4 * radius : 0; }
};


#endif	// _CORNER_SHAPE_H
