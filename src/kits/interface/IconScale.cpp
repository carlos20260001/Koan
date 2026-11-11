/*
 * Copyright 2026, Koan contributors
 * SPDX-License-Identifier: MIT
 */

//! Screen-scale aware icon bitmaps for BIconUtils.


#include <IconUtils.h>

#include <atomic>
#include <math.h>
#include <new>

#include <Application.h>
#include <Bitmap.h>
#include <Screen.h>

#include <InterfacePrivate.h>


// 0 until asked; reset when a window learns the screen changed. Kept out of
// libicon, which app_server links without an app_server connection.
static std::atomic<float> sIconScale(0.0f);


namespace BPrivate {

void
invalidate_icon_scale()
{
	sIconScale.store(0.0f, std::memory_order_relaxed);
}

}	// namespace BPrivate


/*!	\brief Returns the scale icons should be rendered at: the main screen's
	scale, or 1 when there is no app_server connection. The value is cached
	and refreshed after a B_SCREEN_CHANGED.
*/
/*static*/ float
BIconUtils::IconScale()
{
	float scale = sIconScale.load(std::memory_order_relaxed);
	if (scale > 0.0f)
		return scale;

	scale = 1.0f;
	if (be_app != NULL) {
		BScreen screen(B_MAIN_SCREEN_ID);
		float screenScale;
		if (screen.IsValid() && screen.GetScale(&screenScale) == B_OK
			&& screenScale > 0.0f) {
			scale = screenScale;
		}
	}

	sIconScale.store(scale, std::memory_order_relaxed);
	return scale;
}


/*!	\brief Creates a bitmap for an icon of \a logicalSize at IconScale().

	The bitmap is logicalSize times the scale in pixels and its density is
	set, so DrawBitmap() at a point draws it at \a logicalSize and, on a
	screen of that scale, copies it 1:1. Render into it with
	GetVectorIcon() or GetIcon() (pass its pixel width as the icon_size).
	Returns NULL on failure; the caller owns the bitmap.
*/
/*static*/ BBitmap*
BIconUtils::CreateIconBitmap(BSize logicalSize, uint32 flags,
	color_space colorSpace)
{
	float logicalWidth = floorf(logicalSize.width) + 1;
	float logicalHeight = floorf(logicalSize.height) + 1;
	if (logicalWidth < 1 || logicalHeight < 1)
		return NULL;

	float scale = IconScale();
	float width = roundf(logicalWidth * scale);
	float height = roundf(logicalHeight * scale);
	if (width < 1)
		width = 1;
	if (height < 1)
		height = 1;

	BBitmap* bitmap = new(std::nothrow) BBitmap(
		BRect(0, 0, width - 1, height - 1), flags, colorSpace);
	if (bitmap == NULL)
		return NULL;
	if (bitmap->InitCheck() != B_OK) {
		delete bitmap;
		return NULL;
	}

	bitmap->SetDensity(width / logicalWidth);
	return bitmap;
}
