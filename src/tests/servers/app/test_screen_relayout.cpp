/*
 * Copyright 2026, Koan contributors. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

// Host test for the relayout policy app_server applies when a monitor of
// air/OS's display layout changes size or rotation (Desktop::
// _RelayoutWindow(), from _MoveWindowsWithDisplays()). Monitors need not
// start at the desktop's origin.


#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#include "ScreenRelayout.h"


static int sFailures = 0;
static int sChecks = 0;


static void
check(bool condition, const char* what)
{
	sChecks++;
	if (!condition) {
		sFailures++;
		fprintf(stderr, "FAIL: %s\n", what);
	}
}


static BPoint
relayout(BRect footprint, BRect from, BRect to)
{
	relayout_placement placement = RelayoutCapture(footprint, from);
	return RelayoutOrigin(placement, footprint, to);
}


int
main()
{
	// policy
	check(RelayoutShouldMove(true, 0, 0, "application/x-vnd.test"),
		"a normal window moves");
	check(!RelayoutShouldMove(false, 0, 0, NULL), "a hidden window stays");
	check(!RelayoutShouldMove(true, 1024, 0, NULL),
		"the desktop window stays");
	check(!RelayoutShouldMove(true, 0, 1, NULL),
		"a B_NOT_MOVABLE window stays");
	check(!RelayoutShouldMove(true, 0, 0, "application/x-vnd.Be-TSKB"),
		"Deskbar places itself");

	// a second monitor right of the first, 1920x1080 -> 2560x1440
	BRect from(1920, 0, 3839, 1079);
	BRect to(1920, 0, 4479, 1439);

	// anchored to the right edge, 10 pixels in
	BRect right(3329, 8, 3829, 308);
	BPoint origin = relayout(right, from, to);
	check(origin.x == 4479 - 10 - 500, "right edge anchor kept");
	check(origin.y == 8, "top edge anchor kept");

	// centred stays centred
	BRect centred(2779, 390, 2979, 689);
	origin = relayout(centred, from, to);
	check(origin.x == 1920 + (2560 - 201) / 2, "centred horizontally");
	check(origin.y == floorf((1440 - 300) / 2.0f), "centred vertically");

	// left edge on that monitor stays relative to it, not to the desktop
	BRect left(1925, 600, 2124, 699);
	origin = relayout(left, from, to);
	check(origin.x == 1925, "left anchor relative to the monitor");

	// rotation: 1920x1080 -> 1080x1920, a window at the bottom right
	BRect landscape(0, 0, 1919, 1079);
	BRect portrait(0, 0, 1079, 1919);
	BRect corner(1400, 700, 1909, 1069);
	origin = relayout(corner, landscape, portrait);
	check(origin.x == 1080 - 10 - 510, "rotated: right anchor kept");
	check(origin.y == 1920 - 10 - 370, "rotated: bottom anchor kept");

	// shrinking keeps the title reachable
	BRect far(3000, 900, 3799, 1069);
	origin = relayout(far, BRect(0, 0, 3839, 2159), BRect(0, 0, 1279, 719));
	check(origin.x >= 0 && origin.x <= 1279 - 23, "shrunk: on screen (x)");
	check(origin.y >= 0 && origin.y <= 719 - 23, "shrunk: on screen (y)");

	// windows filling the monitor are left to the caller
	check(RelayoutFillsScreen(BRect(1920, 0, 3839, 1079), from),
		"a window filling the monitor is recognised");
	check(!RelayoutFillsScreen(right, from), "a smaller one is not");

	printf("%d checks, %d failed\n", sChecks, sFailures);
	return sFailures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
