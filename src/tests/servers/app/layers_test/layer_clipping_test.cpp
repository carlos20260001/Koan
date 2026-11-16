/*
 * Copyright 2026, Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

/*!	layer_clipping_test [layers]: layers (BeginLayer) whose drawing is
	clipped, by ClipToRect or ConstrainClippingRegion, inside or before the
	layer. Each case draws on a white background in an offscreen bitmap and
	checks pixels; then many clipped layers that fill the whole view are
	timed (each layer's bitmap is as large as its bounding box).
*/


#include <Application.h>
#include <Bitmap.h>
#include <OS.h>
#include <Region.h>
#include <View.h>
#include <stdio.h>
#include <stdlib.h>

static const BRect kBounds(0, 0, 799, 599);
static const BRect kClip(50, 50, 99, 99);

static uint32
pixel(BBitmap* bitmap, int x, int y)
{
	uint8* row = (uint8*)bitmap->Bits() + y * bitmap->BytesPerRow();
	return *(uint32*)(row + x * 4) & 0xffffff;
}

static void
clip(BView* view, int how)
{
	switch (how) {
		case 0:
			view->ClipToRect(kClip);
			break;
		case 1:
		{
			BRegion region(kClip);
			view->ConstrainClippingRegion(&region);
			break;
		}
	}
}

int
main(int argc, char** argv)
{
	BApplication app("application/x-vnd.vos-layer-clipping-test");
	BBitmap* bitmap = new BBitmap(kBounds, B_BITMAP_ACCEPTS_VIEWS, B_RGB32);
	BView* view = new BView(kBounds, "v", B_FOLLOW_NONE, B_WILL_DRAW);
	bitmap->AddChild(view);

	const char* clipNames[] = { "ClipToRect", "ConstrainClippingRegion" };
	const char* placeNames[] = { "no layer", "clip inside layer",
		"clip before layer" };
	int failures = 0;
	for (int how = 0; how < 2; how++) {
		for (int place = 0; place < 3; place++) {
			bitmap->Lock();
			view->SetHighColor(255, 255, 255);
			view->FillRect(kBounds);
			view->PushState();
			if (place != 1)
				clip(view, how);
			if (place > 0)
				view->BeginLayer(255);
			if (place == 1)
				clip(view, how);
			view->SetHighColor(255, 0, 0);
			view->FillRect(kBounds);
			if (place > 0)
				view->EndLayer();
			view->PopState();
			if (how == 1)
				view->ConstrainClippingRegion(NULL);
			view->Sync();
			uint32 inside = pixel(bitmap, 75, 75);
			uint32 outside = pixel(bitmap, 300, 300);
			bitmap->Unlock();
			bool ok = inside == 0xff0000 && outside == 0xffffff;
			printf("%-24s %-18s inside %06x outside %06x %s\n",
				clipNames[how], placeNames[place], (unsigned)inside,
				(unsigned)outside, ok ? "ok" : "WRONG");
			if (!ok)
				failures++;
		}
	}

	int layers = argc > 1 ? atoi(argv[1]) : 300;
	bitmap->Lock();
	bigtime_t start = system_time();
	for (int round = 0; round < 5; round++) {
		for (int i = 0; i < layers; i++) {
			view->PushState();
			view->BeginLayer(128);
			view->ClipToRect(BRect(i % 700, (i * 7) % 500,
				i % 700 + 15, (i * 7) % 500 + 15));
			view->SetHighColor(i & 255, 100, 200);
			view->FillRect(kBounds);
			view->EndLayer();
			view->PopState();
		}
		view->Sync();
	}
	bigtime_t elapsed = system_time() - start;
	bitmap->Unlock();

	printf("%d clipped full-view layers x 5: %.1f ms (%.1f us each)\n",
		layers, elapsed / 1000.0, elapsed / (5.0 * layers));
	printf("%s\n", failures == 0 ? "PASS" : "FAIL");
	delete bitmap;
	return failures;
}
