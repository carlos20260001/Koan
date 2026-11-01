/*
 * Copyright 2026, Koan contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#include <math.h>
#include <stdio.h>

#include "ScreenScaleTransform.h"


static int sFailures = 0;


static void
check(bool condition, const char* name)
{
	if (!condition) {
		printf("FAIL|%s\n", name);
		sFailures++;
	} else
		printf("PASS|%s\n", name);
}


int
main()
{
	const float scales[] = { 1.0f, 1.25f, 1.5f, 1.75f };
	const BRect physicalFrame(0, 0, 1919, 1079);
	const BPoint physicalPoint(777, 331);
	const BRect logicalPixel(10, 20, 10, 20);

	for (uint32 i = 0; i < sizeof(scales) / sizeof(scales[0]); i++) {
		float scale = scales[i];
		BRect logicalFrame
			= ScreenScaleTransform::ToLogicalFrame(physicalFrame, scale);
		BPoint logicalPoint = ScreenScaleTransform::ToLogical(physicalPoint,
			physicalFrame, logicalFrame, scale);
		BPoint roundTripped = ScreenScaleTransform::ToPhysical(logicalPoint,
			physicalFrame, logicalFrame, scale);
		BRect physicalPixel
			= ScreenScaleTransform::ToPhysicalRect(logicalPixel, scale);

		char name[64];
		snprintf(name, sizeof(name), "point-round-trip-%.2f", scale);
		check(fabsf(roundTripped.x - physicalPoint.x) < 0.001f
				&& fabsf(roundTripped.y - physicalPoint.y) < 0.001f,
			name);

		snprintf(name, sizeof(name), "single-pixel-coverage-%.2f", scale);
		check(physicalPixel.IsValid()
				&& physicalPixel.left == floorf(logicalPixel.left * scale)
				&& physicalPixel.right
					== ceilf((logicalPixel.right + 1.0f) * scale) - 1.0f,
			name);

		snprintf(name, sizeof(name), "logical-frame-width-%.2f", scale);
		check(fabsf(logicalFrame.Width() + 1.0f
					- (physicalFrame.Width() + 1.0f) / scale) < 0.001f,
			name);
	}

	check(ScreenScaleTransform::Normalize(0.0f) == 1.0f,
		"invalid-scale-fallback");
	check(ScreenScaleTransform::Normalize(8.0f) == 4.0f,
		"scale-upper-bound");

	printf("SUMMARY|fail=%d\n", sFailures);
	return sFailures == 0 ? 0 : 1;
}
