/*
 * Copyright 2026, Koan contributors. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

// Host test for the rounded window corner rectangles used by decorators:
// runs anywhere with a C++ compiler, no window server required.

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "CornerShape.h"


static int sFailures = 0;


#define CHECK(expression) \
	do { \
		if (!(expression)) { \
			printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #expression); \
			sFailures++; \
		} \
	} while (0)


static const float kScales[] = { 1.0f, 1.25f, 1.5f, 1.75f, 2.0f };
static const int kScaleCount = sizeof(kScales) / sizeof(kScales[0]);


static bool
cut(const int32_t* widths, int32_t x, int32_t y)
{
	return x < widths[y];
}


static void
TestWidthsAtScaleOne()
{
	// Radius 8 at 1.0, worked out by hand: a pixel is cut when its inner
	// corner lies outside the circle.
	int32_t widths[8];
	int32_t rows = CornerShape::CutWidths(8, 1.0f, widths, 8);
	const int32_t expected[8] = { 4, 2, 1, 1, 0, 0, 0, 0 };
	CHECK(rows == 4);
	for (int32_t i = 0; i < 8; i++)
		CHECK(widths[i] == expected[i]);
}


static void
TestWidthsAtEveryScale()
{
	for (int s = 0; s < kScaleCount; s++) {
		const float scale = kScales[s];
		for (int32_t radius = 1; radius <= 16; radius++) {
			int32_t widths[16];
			int32_t rows = CornerShape::CutWidths(radius, scale, widths, 16);

			CHECK(rows <= radius);
			// never reaches the straight edges: row 0 keeps the corner
			// box's last column, and the last row is never cut
			CHECK(widths[0] < radius || radius == 1);
			CHECK(widths[radius - 1] == 0 || radius == 1);
			for (int32_t y = 0; y < radius; y++) {
				CHECK(widths[y] >= 0 && widths[y] <= radius);
				if (y > 0)
					CHECK(widths[y] <= widths[y - 1]);
				for (int32_t x = 0; x < radius; x++)
					CHECK(cut(widths, x, y) == cut(widths, y, x));
			}
			for (int32_t y = radius; y < 16; y++)
				CHECK(widths[y] == 0);

			// Only cells entirely outside the circle are cut: the device
			// pixels cut cover at most the area outside the quarter circle,
			// and at radius 8 and more a good part of it.
			int32_t area = 0;
			for (int32_t y = 0; y < radius; y++) {
				int32_t rowPixels = (int32_t)floorf((y + 1) * scale)
					- (int32_t)floorf(y * scale);
				int32_t cutPixels = (int32_t)floorf(widths[y] * scale);
				area += rowPixels * cutPixels;
			}
			double side = radius * (double)scale;
			double outside = side * side * (1 - M_PI / 4);
			CHECK(area <= outside + 0.001);
			if (radius >= 8)
				CHECK(area >= outside * 0.4);
		}
	}
}


static void
TestCutCellsAreOutsideAtDevicePixels()
{
	// Every device pixel of every cut cell lies entirely outside the circle:
	// its corner nearest to the center does.
	for (int s = 0; s < kScaleCount; s++) {
		const float scale = kScales[s];
		const int32_t radius = 8;
		int32_t widths[8];
		CornerShape::CutWidths(radius, scale, widths, 8);
		double center = radius * scale;
		for (int32_t y = 0; y < radius; y++) {
			for (int32_t x = 0; x < widths[y]; x++) {
				int32_t y0 = (int32_t)floorf(y * scale);
				int32_t y1 = (int32_t)floorf((y + 1) * scale) - 1;
				int32_t x0 = (int32_t)floorf(x * scale);
				int32_t x1 = (int32_t)floorf((x + 1) * scale) - 1;
				for (int32_t py = y0; py <= y1; py++) {
					for (int32_t px = x0; px <= x1; px++) {
						double dx = center - (px + 1);
						double dy = center - (py + 1);
						CHECK(dx * dx + dy * dy > center * center);
					}
				}
			}
		}
	}
}


static void
TestRectsStayInTheCorners()
{
	const int32_t left = 100, top = 50, right = 499, bottom = 349;
	for (int s = 0; s < kScaleCount; s++) {
		CornerRect rects[64];
		int32_t count = CornerShape::CutRects(left, top, right, bottom, 8,
			kScales[s], rects, 64);
		CHECK(count > 0 && count % 4 == 0);

		static bool covered[400][300];
		memset(covered, 0, sizeof(covered));
		for (int32_t i = 0; i < count; i++) {
			const CornerRect& r = rects[i];
			CHECK(r.left <= r.right && r.top <= r.bottom);
			CHECK(r.left >= left && r.right <= right);
			CHECK(r.top >= top && r.bottom <= bottom);
			// inside one of the four 8x8 corner boxes
			bool inLeft = r.right < left + 8;
			bool inRight = r.left > right - 8;
			bool inTop = r.bottom < top + 8;
			bool inBottom = r.top > bottom - 8;
			CHECK((inLeft || inRight) && (inTop || inBottom));
			for (int32_t y = r.top; y <= r.bottom; y++) {
				for (int32_t x = r.left; x <= r.right; x++) {
					CHECK(!covered[x - left][y - top]);
						// no overlaps
					covered[x - left][y - top] = true;
				}
			}
		}

		// the four corners mirror each other
		for (int32_t y = 0; y < 8; y++) {
			for (int32_t x = 0; x < 8; x++) {
				bool tl = covered[x][y];
				CHECK(covered[399 - x][y] == tl);
				CHECK(covered[x][299 - y] == tl);
				CHECK(covered[399 - x][299 - y] == tl);
			}
		}
		// the straight edges between the corners are untouched
		for (int32_t x = 8; x < 400 - 8; x++) {
			CHECK(!covered[x][0]);
			CHECK(!covered[x][299]);
		}
		for (int32_t y = 8; y < 300 - 8; y++) {
			CHECK(!covered[0][y]);
			CHECK(!covered[399][y]);
		}
	}
}


static void
TestRadiusClamp()
{
	CHECK(CornerShape::ClampRadius(8, 400, 300) == 8);
	CHECK(CornerShape::ClampRadius(8.9f, 400, 300) == 8);
	CHECK(CornerShape::ClampRadius(8, 10, 300) == 5);
	CHECK(CornerShape::ClampRadius(8, 300, 7) == 3);
	CHECK(CornerShape::ClampRadius(8, 1, 1) == 0);
	CHECK(CornerShape::ClampRadius(0, 400, 300) == 0);
	CHECK(CornerShape::ClampRadius(-3, 400, 300) == 0);
	CHECK(CornerShape::ClampRadius(500, 4000, 3000) == 64);

	// A tiny window: corners shrink and never meet.
	CornerRect rects[64];
	int32_t count = CornerShape::CutRects(0, 0, 9, 5, 8, 1.0f, rects, 64);
	for (int32_t i = 0; i < count; i++) {
		CHECK(rects[i].right - rects[i].left + 1 <= 3);
		CHECK(rects[i].right <= 9 && rects[i].bottom <= 5);
		bool leftHalf = rects[i].right < 5;
		bool rightHalf = rects[i].left >= 5;
		CHECK(leftHalf || rightHalf);
	}
	CHECK(CornerShape::CutRects(0, 0, 0, 0, 8, 1.0f, rects, 64) == 0);

	// Bad input is harmless.
	CHECK(CornerShape::CutRects(10, 10, 5, 5, 8, 1.0f, rects, 64) == 0);
	CHECK(CornerShape::CutRects(0, 0, 99, 99, 8, 1.0f, NULL, 64) == 0);
	CHECK(CornerShape::CutRects(0, 0, 99, 99, 8, 1.0f, rects, 3) == 0);
	int32_t widths[4];
	CHECK(CornerShape::CutWidths(0, 1.0f, widths, 4) == 0);
	CHECK(CornerShape::CutWidths(8, 0.0f, widths, 4) >= 0);
}


int
main()
{
	TestWidthsAtScaleOne();
	TestWidthsAtEveryScale();
	TestCutCellsAreOutsideAtDevicePixels();
	TestRectsStayInTheCorners();
	TestRadiusClamp();

	if (sFailures != 0) {
		printf("%d check(s) failed\n", sFailures);
		return EXIT_FAILURE;
	}
	printf("test_corner_shape: all checks passed\n");
	return EXIT_SUCCESS;
}
