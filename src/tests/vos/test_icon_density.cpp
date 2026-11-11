/*
 * Copyright 2026, Koan contributors
 * SPDX-License-Identifier: MIT
 */

// BBitmap density and BIconUtils::CreateIconBitmap(): an icon rendered at
// the screen's scale must lay out and draw at its logical size.

#include <stdio.h>

#include <Bitmap.h>
#include <IconUtils.h>


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
	BBitmap plain(BRect(0, 0, 63, 63), B_BITMAP_NO_SERVER_LINK, B_RGBA32);
	check(plain.InitCheck() == B_OK, "bitmap-init");
	check(plain.Density() == 1.0f, "default-density-1");
	check(plain.LogicalBounds() == plain.Bounds(),
		"default-logical-bounds-equal-bounds");

	BBitmap hidpi(BRect(0, 0, 63, 63), B_BITMAP_NO_SERVER_LINK, B_RGBA32);
	hidpi.SetDensity(2.0f);
	check(hidpi.LogicalBounds() == BRect(0, 0, 31, 31),
		"density-2-64px-covers-32");

	BBitmap fractional(BRect(0, 0, 47, 47), B_BITMAP_NO_SERVER_LINK,
		B_RGBA32);
	fractional.SetDensity(1.5f);
	check(fractional.LogicalBounds() == BRect(0, 0, 31, 31),
		"density-1.5-48px-covers-32");

	BBitmap copy(hidpi, B_BITMAP_NO_SERVER_LINK);
	check(copy.Density() == 2.0f, "copy-keeps-density");
	BBitmap assigned(BRect(0, 0, 0, 0), B_BITMAP_NO_SERVER_LINK, B_RGBA32);
	assigned = hidpi;
	check(assigned.Density() == 2.0f, "assignment-keeps-density");

	hidpi.SetDensity(0.0f);
	check(hidpi.Density() == 1.0f, "non-positive-density-resets-to-1");

	// No BApplication: no app_server to ask, so the scale is 1.
	check(BIconUtils::IconScale() == 1.0f, "icon-scale-1-without-app");
	BBitmap* icon = BIconUtils::CreateIconBitmap(BSize(31, 31),
		B_BITMAP_NO_SERVER_LINK);
	check(icon != NULL && icon->Bounds() == BRect(0, 0, 31, 31)
			&& icon->Density() == 1.0f,
		"create-icon-bitmap-at-scale-1");
	delete icon;

	printf("%s|%d failure(s)\n", sFailures == 0 ? "OK" : "FAILED", sFailures);
	return sFailures == 0 ? 0 : 1;
}
