/*
 * Copyright 2026, Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 */


/*!	stale_drawing_bits_test [nomark]: BDirectWindow::MarkDrawingBitsStale().

	A window with B_DIRECT_DEVICE_PIXELS fills what it may draw into with one
	colour (A) in the frame buffer and in app_server's copy of the screen
	(drawing_bits_area), then with another (B) in the frame buffer only, as
	a renderer that presents straight into the screen does, and says so
	(unless "nomark"). Then:
	- the screen is read back (BScreen::GetBitmap()), which at a density
	  other than 100% reads app_server's copy: B where the window is;
	- the window is moved, which moves its pixels in app_server's copy and
	  copies them to the frame buffer: before drawing anything, the window
	  finds B at its new place in the frame buffer.
	Without the mark both give A (where the copy is read), which shows that
	the test tells them apart.
*/


#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <Application.h>
#include <Autolock.h>
#include <Bitmap.h>
#include <DirectWindow.h>
#include <Locker.h>
#include <OS.h>
#include <Screen.h>
#include <View.h>

#include <vector>


static const uint32 kColorA = 0xff2040c0;
static const uint32 kColorB = 0xffc04020;


class StaleWindow : public BDirectWindow {
public:
	StaleWindow(BRect frame)
		:
		BDirectWindow(frame, "stale_drawing_bits_test", B_TITLED_WINDOW,
			B_DIRECT_DEVICE_PIXELS | B_NOT_RESIZABLE),
		fConnects(0),
		fConnected(false),
		fFront(NULL),
		fDrawing(NULL),
		fDrawingClone(-1)
	{
		BView* view = new BView(Bounds(), "content", B_FOLLOW_ALL, 0);
		view->SetViewColor(B_TRANSPARENT_COLOR);
		AddChild(view);
	}

	virtual void DirectConnected(direct_buffer_info* info)
	{
		BAutolock _(fLock);
		int32 mode = info->buffer_state & B_DIRECT_MODE_MASK;
		fConnected = mode != B_DIRECT_STOP;
		if (!fConnected)
			return;
		fFront = (uint8*)info->bits;
		fFrontBPR = info->bytes_per_row;
		fBounds = info->window_bounds;
		fClips.clear();
		for (uint32 i = 0; i < info->clip_list_count; i++)
			fClips.push_back(info->clip_list[i]);
		if (fDrawingClone < 0 && info->drawing_bits_area >= 0) {
			void* address = NULL;
			fDrawingClone = clone_area("stale test drawing bits", &address,
				B_ANY_ADDRESS, B_READ_AREA | B_WRITE_AREA,
				info->drawing_bits_area);
			fDrawing = fDrawingClone >= 0 ? (uint8*)address : NULL;
		}
		fDrawingBPR = info->drawing_bytes_per_row;
		fScale = info->device_scale;
		fConnects++;
	}

	// fills what the window may draw into, in the frame buffer, and in
	// app_server's copy too if asked
	bool Fill(uint32 color, bool drawingBits)
	{
		BAutolock _(fLock);
		if (!fConnected || fFront == NULL || (drawingBits && fDrawing == NULL))
			return false;
		for (size_t i = 0; i < fClips.size(); i++) {
			const clipping_rect& r = fClips[i];
			for (int32 y = r.top; y <= r.bottom; y++) {
				uint32* front = (uint32*)(fFront + y * fFrontBPR);
				uint32* drawing = drawingBits
					? (uint32*)(fDrawing + y * fDrawingBPR) : NULL;
				for (int32 x = r.left; x <= r.right; x++) {
					front[x] = color;
					if (drawing != NULL)
						drawing[x] = color;
				}
			}
		}
		return true;
	}

	void Mark()
	{
		BAutolock _(fLock);
		MarkDrawingBitsStale(fClips.data(), fClips.size());
	}

	// the frame buffer's pixel at the middle of the window
	uint32 FrontMiddle()
	{
		BAutolock _(fLock);
		int32 x = (fBounds.left + fBounds.right) / 2;
		int32 y = (fBounds.top + fBounds.bottom) / 2;
		return ((uint32*)(fFront + y * fFrontBPR))[x];
	}

	int32 Connects() { BAutolock _(fLock); return fConnects; }
	uint32 Scale() { BAutolock _(fLock); return fScale; }

private:
	BLocker				fLock;
	int32				fConnects;
	bool				fConnected;
	uint8*				fFront;
	uint32				fFrontBPR;
	uint8*				fDrawing;
	area_id				fDrawingClone;
	uint32				fDrawingBPR;
	uint32				fScale;
	clipping_rect		fBounds;
	std::vector<clipping_rect> fClips;
};


static bool
close_to(uint32 a, uint32 b)
{
	for (int shift = 0; shift < 24; shift += 8) {
		int d = (int)((a >> shift) & 0xff) - (int)((b >> shift) & 0xff);
		if (d < -2 || d > 2)
			return false;
	}
	return true;
}


static const char*
name_of(uint32 color)
{
	if (close_to(color, kColorA))
		return "A (app_server's old copy)";
	if (close_to(color, kColorB))
		return "B (the frame buffer)";
	return "something else";
}


int
main(int argc, char** argv)
{
	bool mark = argc < 2 || strcmp(argv[1], "nomark") != 0;
	BApplication app("application/x-vnd.vos-stale_drawing_bits_test");

	// small enough for a 1024x768 screen at 200%
	BRect frame(40, 60, 199, 159);
	StaleWindow* window = new StaleWindow(frame);
	window->Show();
	for (int i = 0; i < 100 && window->Connects() == 0; i++)
		snooze(20000);
	snooze(300000);
	if (!window->Fill(kColorA, true)) {
		printf("no direct access to the frame buffer and app_server's copy\n");
		return 1;
	}
	snooze(100000);
	window->Fill(kColorB, false);
	if (mark)
		window->Mark();
	snooze(100000);
	printf("density %" B_PRIu32 "%%, %s\n", window->Scale(),
		mark ? "marked stale" : "not marked");

	int failures = 0;

	// the screen read back
	BScreen screen;
	BBitmap* bitmap = NULL;
	if (screen.GetBitmap(&bitmap, false, &frame) == B_OK && bitmap != NULL) {
		BRect bounds = bitmap->Bounds();
		int32 x = bounds.IntegerWidth() / 2, y = bounds.IntegerHeight() / 2;
		uint32 pixel = *(uint32*)((uint8*)bitmap->Bits()
			+ y * bitmap->BytesPerRow() + x * 4);
		bool expected = mark || window->Scale() <= 100
			? close_to(pixel, kColorB) : close_to(pixel, kColorA);
		printf("screen read back:       %#" B_PRIx32 ", %s%s\n", pixel,
			name_of(pixel), expected ? "" : " -- unexpected");
		if (!expected)
			failures++;
		delete bitmap;
	}

	// moved: app_server moves the pixels in its copy, then copies them out
	int32 connects = window->Connects();
	if (window->Lock()) {
		window->MoveBy(120, 60);
		window->Unlock();
	}
	for (int i = 0; i < 100 && window->Connects() == connects; i++)
		snooze(10000);
	snooze(200000);
	uint32 moved = window->FrontMiddle();
	bool expected = mark ? close_to(moved, kColorB) : close_to(moved, kColorA);
	printf("moved, in frame buffer: %#" B_PRIx32 ", %s%s\n", moved,
		name_of(moved), expected ? "" : " -- unexpected");
	if (!expected)
		failures++;

	if (window->Lock())
		window->Quit();
	printf("%s\n", failures == 0 ? "PASSED" : "FAILED");
	return failures == 0 ? 0 : 1;
}
