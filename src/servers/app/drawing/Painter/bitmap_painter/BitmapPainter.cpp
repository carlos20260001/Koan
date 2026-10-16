/*
 * Copyright 2009, Christian Packmann.
 * Copyright 2008, Andrej Spielmann <andrej.spielmann@seh.ox.ac.uk>.
 * Copyright 2005-2014, Stephan Aßmus <superstippi@gmx.de>.
 * Copyright 2015, Julian Harnath <julian.harnath@rwth-aachen.de>
 * All rights reserved. Distributed under the terms of the MIT License.
 */
#include "BitmapPainter.h"

#include <Bitmap.h>
#include <StackOrHeapArray.h>

#include <agg_image_accessors.h>
#include <agg_pixfmt_rgba.h>
#include <agg_span_image_filter_rgba.h>

#include "DrawBitmapBilinear.h"
#include "DrawBitmapGeneric.h"
#include "DrawBitmapNearestNeighbor.h"
#include "DrawBitmapNoScale.h"
#include "drawing_support.h"
#include "ServerBitmap.h"
#include "SystemPalette.h"


// #define TRACE_BITMAP_PAINTER
#ifdef TRACE_BITMAP_PAINTER
#	define TRACE(x...)		printf(x)
#else
#	define TRACE(x...)
#endif


Painter::BitmapPainter::BitmapPainter(const Painter* painter,
	ServerBitmap* bitmap, uint32 options)
	:
	fPainter(painter),
	fServerBitmap(bitmap),
	fStatus(B_NO_INIT),
	fOptions(options),
	fAreaScale(0)
{
	if (bitmap == NULL || !bitmap->IsValid())
		return;

	// Check if bitmap has valid bits before trying to draw
	uint8* bits = bitmap->Bits();
	if (bits == NULL)
		return;

	fBitmapBounds = bitmap->Bounds();
	fBitmapBounds.OffsetBy(-fBitmapBounds.left, -fBitmapBounds.top);
		// Compensate for the lefttop offset the bitmap bounds might have
		// It has the right size, but put it at B_ORIGIN

	fColorSpace = bitmap->ColorSpace();

	fBitmap.attach(bits, bitmap->Width(), bitmap->Height(),
		bitmap->BytesPerRow());

	fStatus = B_OK;
}


void
Painter::BitmapPainter::SetAreaSampling(float deviceScale, BPoint origin)
{
	fAreaScale = deviceScale;
	fAreaOrigin = origin;
}


void
Painter::BitmapPainter::Draw(const BRect& sourceRect,
	const BRect& destinationRect)
{
	using namespace BitmapPainterPrivate;

	if (fStatus != B_OK)
		return;

	TRACE("BitmapPainter::Draw()\n");
	TRACE("   bitmapBounds = (%.1f, %.1f) - (%.1f, %.1f)\n",
		fBitmapBounds.left, fBitmapBounds.top,
		fBitmapBounds.right, fBitmapBounds.bottom);
	TRACE("   sourceRect = (%.1f, %.1f) - (%.1f, %.1f)\n",
		sourceRect.left, sourceRect.top,
		sourceRect.right, sourceRect.bottom);
	TRACE("   destinationRect = (%.1f, %.1f) - (%.1f, %.1f)\n",
		destinationRect.left, destinationRect.top,
		destinationRect.right, destinationRect.bottom);

	bool success = _DetermineTransform(sourceRect, destinationRect);
	if (!success)
		return;

	if ((fOptions & B_TILE_BITMAP) == 0) {
		// optimized version for no scale in CMAP8 or RGB32 OP_OVER
		if (!_HasScale() && !_HasAffineTransform() && !_HasAlphaMask()) {
			if (fColorSpace == B_CMAP8) {
				if (fPainter->fDrawingMode == B_OP_COPY) {
					DrawBitmapNoScale<CMap8Copy> drawNoScale;
					drawNoScale.Draw(fPainter->fInternal, fBitmap, 1, fOffset,
						fDestinationRect);
					return;
				}
				if (fPainter->fDrawingMode == B_OP_OVER) {
					DrawBitmapNoScale<CMap8Over> drawNoScale;
					drawNoScale.Draw(fPainter->fInternal, fBitmap, 1, fOffset,
						fDestinationRect);
					return;
				}
			} else if (fColorSpace == B_RGB32) {
				if (fPainter->fDrawingMode == B_OP_OVER) {
					DrawBitmapNoScale<Bgr32Over> drawNoScale;
					drawNoScale.Draw(fPainter->fInternal, fBitmap, 4, fOffset,
						fDestinationRect);
					return;
				}
			}
		}
	}

	ObjectDeleter<BBitmap> convertedBitmapDeleter;
	_ConvertColorSpace(convertedBitmapDeleter);

	if ((fOptions & B_TILE_BITMAP) == 0) {
		// optimized version if there is no scale
		if (!_HasScale() && !_HasAffineTransform() && !_HasAlphaMask()) {
			if (fPainter->fDrawingMode == B_OP_COPY) {
				DrawBitmapNoScale<Bgr32Copy> drawNoScale;
				drawNoScale.Draw(fPainter->fInternal, fBitmap, 4, fOffset,
					fDestinationRect);
				return;
			}
			if (fPainter->fDrawingMode == B_OP_OVER
				|| (fPainter->fDrawingMode == B_OP_ALPHA
					 && fPainter->fAlphaSrcMode == B_PIXEL_ALPHA
					 && fPainter->fAlphaFncMode == B_ALPHA_OVERLAY)) {
				DrawBitmapNoScale<Bgr32Alpha> drawNoScale;
				drawNoScale.Draw(fPainter->fInternal, fBitmap, 4, fOffset,
					fDestinationRect);
				return;
			}
		}

		if (!_HasScale() && !_HasAffineTransform() && _HasAlphaMask()) {
			if (fPainter->fDrawingMode == B_OP_COPY) {
				DrawBitmapNoScale<Bgr32CopyMasked> drawNoScale;
				drawNoScale.Draw(fPainter->fInternal, fBitmap, 4, fOffset,
					fDestinationRect);
				return;
			}
		}

		// enlarged by the device scale alone, OP_COPY only
		if (fAreaScale > 1 && fPainter->fDrawingMode == B_OP_COPY
			&& _HasScale() && !_HasAffineTransform() && !_HasAlphaMask()
			&& _DrawAreaSampled(sourceRect)) {
			return;
		}

		// bilinear and nearest-neighbor scaled, OP_COPY only
		if (fPainter->fDrawingMode == B_OP_COPY
			&& !_HasAffineTransform() && !_HasAlphaMask()) {
			if ((fOptions & B_FILTER_BITMAP_BILINEAR) != 0) {
				DrawBitmapBilinear<ColorTypeRgb, DrawModeCopy> drawBilinear;
				drawBilinear.Draw(fPainter, fPainter->fInternal,
					fBitmap, fOffset, fScaleX, fScaleY, fDestinationRect);
			} else {
				DrawBitmapNearestNeighborCopy::Draw(fPainter, fPainter->fInternal,
					fBitmap, fOffset, fScaleX, fScaleY, fDestinationRect);
			}
			return;
		}

		if (fPainter->fDrawingMode == B_OP_ALPHA
			&& fPainter->fAlphaSrcMode == B_PIXEL_ALPHA
			&& fPainter->fAlphaFncMode == B_ALPHA_OVERLAY
			&& !_HasAffineTransform() && !_HasAlphaMask()
			&& (fOptions & B_FILTER_BITMAP_BILINEAR) != 0) {
			DrawBitmapBilinear<ColorTypeRgba, DrawModeAlphaOverlay> drawBilinear;
			drawBilinear.Draw(fPainter, fPainter->fInternal,
				fBitmap, fOffset, fScaleX, fScaleY, fDestinationRect);
			return;
		}
	}

	if ((fOptions & B_TILE_BITMAP) != 0) {
		DrawBitmapGeneric<Tile>::Draw(fPainter, fPainter->fInternal, fBitmap,
			fOffset, fScaleX, fScaleY, fDestinationRect, fOptions);
	} else {
		// for all other cases (non-optimized drawing mode or scaled drawing)
		DrawBitmapGeneric<Fill>::Draw(fPainter, fPainter->fInternal, fBitmap,
			fOffset, fScaleX, fScaleY, fDestinationRect, fOptions);
	}
}


bool
Painter::BitmapPainter::_DetermineTransform(BRect sourceRect,
	const BRect& destinationRect)
{
	if (!fPainter->fValidClipping
		|| !sourceRect.IsValid()
		|| ((fOptions & B_TILE_BITMAP) == 0
			&& !sourceRect.Intersects(fBitmapBounds))
		|| !destinationRect.IsValid()) {
		return false;
	}

	fDestinationRect = destinationRect;

	if (!fPainter->fSubpixelPrecise) {
		align_rect_to_pixels(&sourceRect);
		align_rect_to_pixels(&fDestinationRect);
	}

	if((fOptions & B_TILE_BITMAP) == 0) {
		fScaleX = (fDestinationRect.Width() + 1) / (sourceRect.Width() + 1);
		fScaleY = (fDestinationRect.Height() + 1) / (sourceRect.Height() + 1);

		if (fScaleX == 0.0 || fScaleY == 0.0)
			return false;

		// constrain source rect to bitmap bounds and transfer the changes to
		// the destination rect with the right scale
		if (sourceRect.left < fBitmapBounds.left) {
			float diff = fBitmapBounds.left - sourceRect.left;
			fDestinationRect.left += diff * fScaleX;
			sourceRect.left = fBitmapBounds.left;
		}
		if (sourceRect.top < fBitmapBounds.top) {
			float diff = fBitmapBounds.top - sourceRect.top;
			fDestinationRect.top += diff * fScaleY;
			sourceRect.top = fBitmapBounds.top;
		}
		if (sourceRect.right > fBitmapBounds.right) {
			float diff = sourceRect.right - fBitmapBounds.right;
			fDestinationRect.right -= diff * fScaleX;
			sourceRect.right = fBitmapBounds.right;
		}
		if (sourceRect.bottom > fBitmapBounds.bottom) {
			float diff = sourceRect.bottom - fBitmapBounds.bottom;
			fDestinationRect.bottom -= diff * fScaleY;
			sourceRect.bottom = fBitmapBounds.bottom;
		}
	} else {
		fScaleX = 1.0;
		fScaleY = 1.0;
	}

	fOffset.x = fDestinationRect.left - sourceRect.left;
	fOffset.y = fDestinationRect.top - sourceRect.top;

	return true;
}


/*!	Enlarges the bitmap by the device scale with area sampling: each buffer
	pixel is the average of the bitmap pixels it covers, at their exact
	fractional positions. The nearest pixel makes every other row and column
	of a bitmap twice as wide at 150 or 175 percent, so its one pixel lines
	and the strokes of text drawn into it came out one or two pixels wide;
	bilinear filtering is even but soft. Here a buffer pixel inside one bitmap
	pixel takes its colour unchanged and only the pixels on their borders are
	mixed, so the picture stays as sharp as the scale allows and every bitmap
	pixel gets the same width. Positions come from the whole desktop's grid,
	not from the rectangle drawn, so redrawing part of an offscreen bitmap
	gives the same pixels as drawing all of it.
*/
bool
Painter::BitmapPainter::_DrawAreaSampled(const BRect& sourceRect)
{
	using namespace BitmapPainterPrivate;

	// A buffer pixel on the edge of the part drawn also covers the bitmap
	// pixel next to it: it is mixed in as in a drawing of the whole bitmap,
	// which an update of part of it has to match. Only the bitmap's own
	// edges are clamped.
	if (!sourceRect.Intersects(fBitmapBounds))
		return false;
	const int32 firstColumn = (int32)fBitmapBounds.left;
	const int32 lastColumn = (int32)fBitmapBounds.right;
	const int32 firstRow = (int32)fBitmapBounds.top;
	const int32 lastRow = (int32)fBitmapBounds.bottom;

	// only what the clipping leaves of the destination needs weights
	BRect destination = fDestinationRect;
	const BRegion& clipping = *fPainter->ClippingRegion();
	BRect drawn = destination & clipping.Frame();
	if (!drawn.IsValid())
		return true;
	const int32 left = (int32)drawn.left;
	const int32 top = (int32)drawn.top;
	const int32 width = (int32)drawn.right - left + 1;
	const int32 height = (int32)drawn.bottom - top + 1;
	if (lastColumn * 4 > 65535 || lastRow > 65535)
		return false;

	BStackOrHeapArray<FilterInfo, 1024> columns(width);
	BStackOrHeapArray<FilterInfo, 1024> rows(height);
	if (!columns.IsValid() || !rows.IsValid())
		return false;

	const float scale = fAreaScale;
	struct Axis {
		static void Weights(FilterInfo* infos, int32 count, int32 start,
			float origin, float scale, int32 first, int32 last, int32 factor)
		{
			for (int32 i = 0; i < count; i++) {
				// the buffer pixel's span in bitmap pixels: above a scale
				// of one it covers two of them at most
				const float from = (start + i - origin) / scale;
				const float to = (start + i + 1 - origin) / scale;
				int32 index = (int32)floorf(from);
				float weight = (min_c(index + 1.0f, to) - from) * scale;
				if (index < first) {
					index = first;
					weight = 1;
				} else if (index >= last) {
					index = last;
					weight = 1;
				}
				infos[i].index = index * factor;
				infos[i].weight = (uint16)(min_c(weight, 1.0f) * 255 + 0.5f);
			}
		}
	};
	Axis::Weights(columns, width, left, fAreaOrigin.x, scale, firstColumn,
		lastColumn, 4);
	Axis::Weights(rows, height, top, fAreaOrigin.y, scale, firstRow, lastRow,
		1);

	FilterData filterData;
	filterData.fWeightsX = columns;
	filterData.fWeightsY = rows;
	filterData.fIndexOffsetX = left - (int32)destination.left;
	filterData.fIndexOffsetY = top - (int32)destination.top;

	BilinearSeparable painter;
	painter.Draw(fPainter->fInternal, destination, &fBitmap, filterData);
	return true;
}


bool
Painter::BitmapPainter::_HasScale()
{
	return fScaleX != 1.0 || fScaleY != 1.0;
}


bool
Painter::BitmapPainter::_HasAffineTransform()
{
	// the device scale is already in the destination rectangle
	return !fPainter->fIdentityViewTransform;
}


bool
Painter::BitmapPainter::_HasAlphaMask()
{
	return fPainter->fInternal.fMaskedUnpackedScanline != NULL;
}


void
Painter::BitmapPainter::_ConvertColorSpace(
	ObjectDeleter<BBitmap>& convertedBitmapDeleter)
{
	if (fColorSpace == B_RGBA32)
		return;

	if (fColorSpace == B_RGB32
		&& (fPainter->fDrawingMode == B_OP_COPY
#if 1
// Enabling this would make the behavior compatible to BeOS, which
// treats B_RGB32 bitmaps as B_RGB*A*32 bitmaps in B_OP_ALPHA - unlike in
// all other drawing modes, where B_TRANSPARENT_MAGIC_RGBA32 is handled.
// B_RGB32 bitmaps therefore don't draw correctly on BeOS if they actually
// use this color, unless the alpha channel contains 255 for all other
// pixels, which is inconsistent.
		|| fPainter->fDrawingMode == B_OP_ALPHA
#endif
		)) {
		return;
	}

	BBitmap* conversionBitmap = new(std::nothrow) BBitmap(fBitmapBounds,
		B_BITMAP_NO_SERVER_LINK, B_RGBA32);
	if (conversionBitmap == NULL) {
		fprintf(stderr, "BitmapPainter::_ConvertColorSpace() - "
			"out of memory for creating temporary conversion bitmap\n");
		return;
	}
	convertedBitmapDeleter.SetTo(conversionBitmap);

	status_t err = conversionBitmap->ImportBits(fBitmap.buf(),
		fBitmap.height() * fBitmap.stride(),
		fBitmap.stride(), 0, fColorSpace);
	if (err < B_OK) {
		fprintf(stderr, "BitmapPainter::_ConvertColorSpace() - "
			"colorspace conversion failed: %s\n", strerror(err));
		return;
	}

	// the original bitmap might have had some of the
	// transaparent magic colors set that we now need to
	// make transparent in our RGBA32 bitmap again.
	switch (fColorSpace) {
		case B_RGB32:
			_TransparentMagicToAlpha((uint32 *)fBitmap.buf(),
				fBitmap.width(), fBitmap.height(),
				fBitmap.stride(), B_TRANSPARENT_MAGIC_RGBA32,
				conversionBitmap);
			break;

		// TODO: not sure if this applies to B_RGBA15 too. It
		// should not because B_RGBA15 actually has an alpha
		// channel itself and it should have been preserved
		// when importing the bitmap. Maybe it applies to
		// B_RGB16 though?
		case B_RGB15:
			_TransparentMagicToAlpha((uint16 *)fBitmap.buf(),
				fBitmap.width(), fBitmap.height(),
				fBitmap.stride(), B_TRANSPARENT_MAGIC_RGBA15,
				conversionBitmap);
			break;

		default:
			break;
	}

	uint8* convertedBits = (uint8*)conversionBitmap->Bits();
	if (convertedBits == NULL)
		return;

	fBitmap.attach(convertedBits,
		(uint32)fBitmapBounds.IntegerWidth() + 1,
		(uint32)fBitmapBounds.IntegerHeight() + 1,
		conversionBitmap->BytesPerRow());
}


template<typename sourcePixel>
void
Painter::BitmapPainter::_TransparentMagicToAlpha(sourcePixel* buffer,
	uint32 width, uint32 height, uint32 sourceBytesPerRow,
	sourcePixel transparentMagic, BBitmap* output)
{
	uint8* sourceRow = (uint8*)buffer;
	uint8* destRow = (uint8*)output->Bits();
	uint32 destBytesPerRow = output->BytesPerRow();

	for (uint32 y = 0; y < height; y++) {
		sourcePixel* pixel = (sourcePixel*)sourceRow;
		uint32* destPixel = (uint32*)destRow;
		for (uint32 x = 0; x < width; x++, pixel++, destPixel++) {
			if (*pixel == transparentMagic)
				*destPixel &= 0x00ffffff;
		}

		sourceRow += sourceBytesPerRow;
		destRow += destBytesPerRow;
	}
}
