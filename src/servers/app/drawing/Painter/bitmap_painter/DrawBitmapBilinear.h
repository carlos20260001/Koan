/*
 * Copyright 2009, Christian Packmann.
 * Copyright 2008, Andrej Spielmann <andrej.spielmann@seh.ox.ac.uk>.
 * Copyright 2005-2014, Stephan Aßmus <superstippi@gmx.de>.
 * Copyright 2015, Julian Harnath <julian.harnath@rwth-aachen.de>
 * All rights reserved. Distributed under the terms of the MIT License.
 */
#ifndef DRAW_BITMAP_BILINEAR_H
#define DRAW_BITMAP_BILINEAR_H

#include "Painter.h"

#include <stdlib.h>
#include <string.h>
#include <typeinfo>

#ifdef __SSE2__
#	include <emmintrin.h>
#endif


// Prototypes for assembler routines
extern "C" {
	void bilinear_scale_xloop_mmxsse(const uint8* src, void* dst,
		void* xWeights, uint32 xmin, uint32 xmax, uint32 wTop, uint32 srcBPR);
}


extern uint32 gSIMDFlags;


namespace BitmapPainterPrivate {


struct FilterInfo {
	uint16 index;	// index into source bitmap row/column
	uint16 weight;	// weight of the pixel at index [0..255]
};


struct FilterData {
	FilterInfo* fWeightsX;
	FilterInfo* fWeightsY;
	uint32 fIndexOffsetX;
	uint32 fIndexOffsetY;
};


template<class OptimizedVersion>
struct DrawBitmapBilinearOptimized {
	void Draw(PainterAggInterface& aggInterface, const BRect& destinationRect,
		agg::rendering_buffer* bitmap, const FilterData& filterData)
	{
		fSource = bitmap;
		fSourceBytesPerRow = bitmap->stride();
		fDestination = NULL;
		fDestinationBytesPerRow = aggInterface.fBuffer.stride();
		fWeightsX = filterData.fWeightsX;
		fWeightsY = filterData.fWeightsY;

		const int32 left = (int32)destinationRect.left;
		const int32 top = (int32)destinationRect.top;
		const int32 right = (int32)destinationRect.right;
		const int32 bottom = (int32)destinationRect.bottom;

		renderer_base& baseRenderer = aggInterface.fBaseRenderer;

		// iterate over clipping boxes
		baseRenderer.first_clip_box();
		do {
			const int32 x1 = max_c(baseRenderer.xmin(), left);
			const int32 x2 = min_c(baseRenderer.xmax(), right);
			if (x1 > x2)
				continue;

			int32 y1 = max_c(baseRenderer.ymin(), top);
			int32 y2 = min_c(baseRenderer.ymax(), bottom);
			if (y1 > y2)
				continue;

			// buffer offset into destination
			fDestination = aggInterface.fBuffer.row_ptr(y1) + x1 * 4;

			// x and y are needed as indices into the weight arrays, so the
			// offset into the target buffer needs to be compensated
			const int32 xIndexL = x1 - left - filterData.fIndexOffsetX;
			const int32 xIndexR = x2 - left - filterData.fIndexOffsetX;
			y1 -= top + filterData.fIndexOffsetY;
			y2 -= top + filterData.fIndexOffsetY;

			//printf("x: %ld - %ld\n", xIndexL, xIndexR);
			//printf("y: %ld - %ld\n", y1, y2);

			static_cast<OptimizedVersion*>(this)->DrawToClipRect(
				xIndexL, xIndexR, y1, y2);

		} while (baseRenderer.next_clip_box());
	}

protected:
	agg::rendering_buffer*	fSource;
	uint32					fSourceBytesPerRow;
	uint8*					fDestination;
	uint32					fDestinationBytesPerRow;
	FilterInfo*				fWeightsX;
	FilterInfo*				fWeightsY;
};


struct ColorTypeRgb {
	static void
	Interpolate(uint32* t, const uint8* s, uint32 sourceBytesPerRow,
		uint16 wLeft, uint16 wTop, uint16 wRight, uint16 wBottom)
	{
		// left and right of top row
		t[0] = (s[0] * wLeft + s[4] * wRight) * wTop;
		t[1] = (s[1] * wLeft + s[5] * wRight) * wTop;
		t[2] = (s[2] * wLeft + s[6] * wRight) * wTop;

		// left and right of bottom row
		s += sourceBytesPerRow;
		t[0] += (s[0] * wLeft + s[4] * wRight) * wBottom;
		t[1] += (s[1] * wLeft + s[5] * wRight) * wBottom;
		t[2] += (s[2] * wLeft + s[6] * wRight) * wBottom;

		t[0] >>= 16;
		t[1] >>= 16;
		t[2] >>= 16;
	}

	static void
	InterpolateLastColumn(uint32* t, const uint8* s, const uint8* sBottom,
		uint16 wTop, uint16 wBottom)
	{
		t[0] = (s[0] * wTop + sBottom[0] * wBottom) >> 8;
		t[1] = (s[1] * wTop + sBottom[1] * wBottom) >> 8;
		t[2] = (s[2] * wTop + sBottom[2] * wBottom) >> 8;
	}

	static void
	InterpolateLastRow(uint32* t, const uint8* s, uint16 wLeft,
		uint16 wRight)
	{
		t[0] = (s[0] * wLeft + s[4] * wRight) >> 8;
		t[1] = (s[1] * wLeft + s[5] * wRight) >> 8;
		t[2] = (s[2] * wLeft + s[6] * wRight) >> 8;
	}
};


struct ColorTypeRgba {
	static void
	Interpolate(uint32* t, const uint8* s, uint32 sourceBytesPerRow,
		uint16 wLeft, uint16 wTop, uint16 wRight, uint16 wBottom)
	{
		// left and right of top row
		t[0] = (s[0] * wLeft + s[4] * wRight) * wTop;
		t[1] = (s[1] * wLeft + s[5] * wRight) * wTop;
		t[2] = (s[2] * wLeft + s[6] * wRight) * wTop;
		t[3] = (s[3] * wLeft + s[7] * wRight) * wTop;

		// left and right of bottom row
		s += sourceBytesPerRow;

		t[0] += (s[0] * wLeft + s[4] * wRight) * wBottom;
		t[1] += (s[1] * wLeft + s[5] * wRight) * wBottom;
		t[2] += (s[2] * wLeft + s[6] * wRight) * wBottom;
		t[3] += (s[3] * wLeft + s[7] * wRight) * wBottom;

		t[0] >>= 16;
		t[1] >>= 16;
		t[2] >>= 16;
		t[3] >>= 16;
	}

	static void
	InterpolateLastColumn(uint32* t, const uint8* s, const uint8* sBottom,
		uint16 wTop, uint16 wBottom)
	{
		t[0] = (s[0] * wTop + sBottom[0] * wBottom) >> 8;
		t[1] = (s[1] * wTop + sBottom[1] * wBottom) >> 8;
		t[2] = (s[2] * wTop + sBottom[2] * wBottom) >> 8;
		t[3] = (s[3] * wTop + sBottom[3] * wBottom) >> 8;
	}

	static void
	InterpolateLastRow(uint32* t, const uint8* s, uint16 wLeft,
		uint16 wRight)
	{
		t[0] = (s[0] * wLeft + s[4] * wRight) >> 8;
		t[1] = (s[1] * wLeft + s[5] * wRight) >> 8;
		t[2] = (s[2] * wLeft + s[6] * wRight) >> 8;
		t[3] = (s[3] * wLeft + s[7] * wRight) >> 8;
	}
};


struct DrawModeCopy {
	static void
	Blend(uint8*& d, uint32* t)
	{
		d[0] = t[0];
		d[1] = t[1];
		d[2] = t[2];
		d += 4;
	}
};


struct DrawModeAlphaOverlay {
	static void
	Blend(uint8*& d, uint32* t)
	{
		uint8 t0 = t[0];
		uint8 t1 = t[1];
		uint8 t2 = t[2];
		uint8 t3 = t[3];

		if (t3 == 255) {
			d[0] = t0;
			d[1] = t1;
			d[2] = t2;
		} else {
			d[0] = ((t0 - d[0]) * t3 + (d[0] << 8)) >> 8;
			d[1] = ((t1 - d[1]) * t3 + (d[1] << 8)) >> 8;
			d[2] = ((t2 - d[2]) * t3 + (d[2] << 8)) >> 8;
		}

		d += 4;
	}
};


template<class ColorType, class DrawMode>
struct BilinearDefault :
	DrawBitmapBilinearOptimized<BilinearDefault<ColorType, DrawMode> > {

	void DrawToClipRect(int32 xIndexL, int32 xIndexR, int32 y1, int32 y2)
	{
		// In this mode we anticipate many pixels which need filtering,
		// there are no special cases for direct hit pixels except for
		// the last column/row and the right/bottom corner pixel.

		// The last column/row handling does not need to be performed
		// for all clipping rects!
		int32 yMax = y2;
		if (this->fWeightsY[yMax].weight == 255)
			yMax--;
		int32 xIndexMax = xIndexR;
		if (this->fWeightsX[xIndexMax].weight == 255)
			xIndexMax--;

		for (; y1 <= yMax; y1++) {
			// cache the weight of the top and bottom row
			const uint16 wTop = this->fWeightsY[y1].weight;
			const uint16 wBottom = 255 - this->fWeightsY[y1].weight;

			// buffer offset into source (top row)
			const uint8* src = this->fSource->row_ptr(
				this->fWeightsY[y1].index);

			// buffer handle for destination to be incremented per
			// pixel
			uint8* d = this->fDestination;

			for (int32 x = xIndexL; x <= xIndexMax; x++) {
				const uint8* s = src + this->fWeightsX[x].index;

				// calculate the weighted sum of all four
				// interpolated pixels
				const uint16 wLeft = this->fWeightsX[x].weight;
				const uint16 wRight = 255 - wLeft;

				uint32 t[4];

				if (this->fSource->height() > 1) {
					ColorType::Interpolate(&t[0], s, this->fSourceBytesPerRow,
						wLeft, wTop, wRight, wBottom);
				} else {
					ColorType::InterpolateLastRow(&t[0], s,  wLeft, wRight);
				}
				DrawMode::Blend(d, &t[0]);
			}
			// last column of pixels if necessary
			if (xIndexMax < xIndexR && this->fSource->height() > 1) {
				const uint8* s = src + this->fWeightsX[xIndexR].index;
				const uint8* sBottom = s + this->fSourceBytesPerRow;

				uint32 t[4];
				ColorType::InterpolateLastColumn(&t[0], s, sBottom, wTop,
					wBottom);
				DrawMode::Blend(d, &t[0]);
			}

			this->fDestination += this->fDestinationBytesPerRow;
		}

		// last row of pixels if necessary
		// buffer offset into source (bottom row)
		const uint8* src
			= this->fSource->row_ptr(this->fWeightsY[y2].index);
		// buffer handle for destination to be incremented per pixel
		uint8* d = this->fDestination;

		if (yMax < y2) {
			for (int32 x = xIndexL; x <= xIndexMax; x++) {
				const uint8* s = src + this->fWeightsX[x].index;
				const uint16 wLeft = this->fWeightsX[x].weight;
				const uint16 wRight = 255 - wLeft;
				uint32 t[4];
				ColorType::InterpolateLastRow(&t[0], s, wLeft, wRight);
				DrawMode::Blend(d, &t[0]);
			}
		}

		// pixel in bottom right corner if necessary
		if (yMax < y2 && xIndexMax < xIndexR) {
			const uint8* s = src + this->fWeightsX[xIndexR].index;
			uint32 t[4] = {s[0], s[1], s[2], s[3]};
			DrawMode::Blend(d, &t[0]);
		}
	}
};


struct BilinearLowFilterRatio :
	DrawBitmapBilinearOptimized<BilinearLowFilterRatio> {
	void DrawToClipRect(int32 xIndexL, int32 xIndexR, int32 y1, int32 y2)
	{
		// In this mode, we anticipate to hit many destination pixels
		// that map directly to a source pixel, we have more branches
		// in the inner loop but save time because of the special
		// cases. If there are too few direct hit pixels, the branches
		// only waste time.

		for (; y1 <= y2; y1++) {
			// cache the weight of the top and bottom row
			const uint16 wTop = fWeightsY[y1].weight;
			const uint16 wBottom = 255 - fWeightsY[y1].weight;

			// buffer offset into source (top row)
			const uint8* src = fSource->row_ptr(fWeightsY[y1].index);
			// buffer handle for destination to be incremented per
			// pixel
			uint8* d = fDestination;

			if (wTop == 255) {
				for (int32 x = xIndexL; x <= xIndexR; x++) {
					const uint8* s = src + fWeightsX[x].index;
					// This case is important to prevent out
					// of bounds access at bottom edge of the source
					// bitmap. If the scale is low and integer, it will
					// also help the speed.
					if (fWeightsX[x].weight == 255) {
						// As above, but to prevent out of bounds
						// on the right edge.
						*(uint32*)d = *(uint32*)s;
					} else {
						// Only the left and right pixels are
						// interpolated, since the top row has 100%
						// weight.
						const uint16 wLeft = fWeightsX[x].weight;
						const uint16 wRight = 255 - wLeft;
						d[0] = (s[0] * wLeft + s[4] * wRight) >> 8;
						d[1] = (s[1] * wLeft + s[5] * wRight) >> 8;
						d[2] = (s[2] * wLeft + s[6] * wRight) >> 8;
					}
					d += 4;
				}
			} else {
				for (int32 x = xIndexL; x <= xIndexR; x++) {
					const uint8* s = src + fWeightsX[x].index;
					if (fWeightsX[x].weight == 255) {
						// Prevent out of bounds access on the right
						// edge or simply speed up.
						const uint8* sBottom = s + fSourceBytesPerRow;
						d[0] = (s[0] * wTop + sBottom[0] * wBottom)
							>> 8;
						d[1] = (s[1] * wTop + sBottom[1] * wBottom)
							>> 8;
						d[2] = (s[2] * wTop + sBottom[2] * wBottom)
							>> 8;
					} else {
						// calculate the weighted sum of all four
						// interpolated pixels
						const uint16 wLeft = fWeightsX[x].weight;
						const uint16 wRight = 255 - wLeft;
						// left and right of top row
						uint32 t0 = (s[0] * wLeft + s[4] * wRight)
							* wTop;
						uint32 t1 = (s[1] * wLeft + s[5] * wRight)
							* wTop;
						uint32 t2 = (s[2] * wLeft + s[6] * wRight)
							* wTop;

						// left and right of bottom row
						s += fSourceBytesPerRow;
						t0 += (s[0] * wLeft + s[4] * wRight) * wBottom;
						t1 += (s[1] * wLeft + s[5] * wRight) * wBottom;
						t2 += (s[2] * wLeft + s[6] * wRight) * wBottom;

						d[0] = t0 >> 16;
						d[1] = t1 >> 16;
						d[2] = t2 >> 16;
					}
					d += 4;
				}
			}
			fDestination += fDestinationBytesPerRow;
		}
	}
};



// Filters in two passes for each destination row: first its two source rows
// are blended into one, once per source pixel, then each destination pixel
// blends two neighbours of that row. Both passes work on whole pixels and
// their weights add up to 256, so a flat colour stays exact (the per pixel
// versions darken white to 253). This is what a scaled desktop does to every
// bitmap drawn with B_FILTER_BITMAP_BILINEAR, a GL view or a video frame for
// example. With SSE2 (every x86_64), both passes do four pixels at a time.
struct BilinearSeparable : DrawBitmapBilinearOptimized<BilinearSeparable> {
	BilinearSeparable()
		:
		fRow(NULL),
		fRowSize(0),
		fColumns(NULL),
		fColumnsSize(0),
		fOverlay(false)
	{
	}

	//! Blends the bitmap's pixels over the destination by their alpha
	//! (B_OP_ALPHA, B_PIXEL_ALPHA, B_ALPHA_OVERLAY) instead of copying them.
	void SetOverlay(bool overlay)
	{
		fOverlay = overlay;
	}

	~BilinearSeparable()
	{
		free(fRow);
		free(fColumns);
	}

	void DrawToClipRect(int32 xIndexL, int32 xIndexR, int32 y1, int32 y2)
	{
		const int32 firstColumn = fWeightsX[xIndexL].index / 4;
		const int32 lastColumn = min_c(fWeightsX[xIndexR].index / 4 + 1,
			(int32)fSource->width() - 1);
		const int32 count = lastColumn - firstColumn + 1;
		const int32 lastRow = (int32)fSource->height() - 1;
		const int32 width = xIndexR - xIndexL + 1;

		// the row gets one more pixel, the right neighbour of its last one
		if (count < 1 || width < 1 || !_Reserve(count + 1, width))
			return;

		// where each destination pixel finds its left neighbour in the row,
		// and the weights of that one and the right one
		for (int32 x = 0; x < width; x++) {
			const FilterInfo& info = fWeightsX[xIndexL + x];
			const uint32 left = _Weight(info.weight);
			fColumns[x].offset = info.index / 4 - firstColumn;
			fColumns[x].weights = left | (256 - left) << 16;
		}

		for (int32 y = y1; y <= y2; y++) {
			const int32 sourceY = fWeightsY[y].index;
			const uint32 wTop = _Weight(fWeightsY[y].weight);
			const uint32* top = (const uint32*)fSource->row_ptr(sourceY)
				+ firstColumn;
			const uint32* bottom = wTop < 256 && sourceY < lastRow
				? (const uint32*)fSource->row_ptr(sourceY + 1) + firstColumn
				: NULL;
			if (fOverlay) {
				// mixed with their alpha applied, or the colour of
				// transparent pixels would show at the edges
				for (int32 i = 0; i < count; i++) {
					fRow[i] = bottom != NULL
						? _Blend(_Premultiplied(top[i]),
							_Premultiplied(bottom[i]), wTop, 256 - wTop)
						: _Premultiplied(top[i]);
				}
			} else if (bottom != NULL)
				_BlendRows(top, bottom, wTop, count);
			else
				memcpy(fRow, top, count * 4);
			fRow[count] = fRow[count - 1];

			if (fOverlay)
				_BlendColumnsOver((uint32*)fDestination, width);
			else
				_BlendColumns((uint32*)fDestination, width);
			fDestination += fDestinationBytesPerRow;
		}
	}

private:
	struct Column {
		int32	offset;
		uint32	weights;	// left | right << 16
	};

	// FilterInfo weights are [0..255] for the left or top pixel, with
	// 255 - weight for the other one: [0..256] here, adding up to 256.
	static inline uint32 _Weight(uint16 weight)
	{
		return (weight * 257 + 128) >> 8;
	}

	static inline uint32 _Blend(uint32 a, uint32 b, uint32 wA, uint32 wB)
	{
		// two 8 bit channels in each 16 bit lane: 255 * 256 still fits
		const uint32 blueRed = (((a & 0x00ff00ff) * wA + (b & 0x00ff00ff) * wB)
			>> 8) & 0x00ff00ff;
		const uint32 greenAlpha = (((a >> 8) & 0x00ff00ff) * wA
			+ ((b >> 8) & 0x00ff00ff) * wB) & 0xff00ff00;
		return blueRed | greenAlpha;
	}

	void _BlendRows(const uint32* top, const uint32* bottom, uint32 wTop,
		int32 count)
	{
		const uint32 wBottom = 256 - wTop;
		int32 i = 0;
#ifdef __SSE2__
		const __m128i zero = _mm_setzero_si128();
		const __m128i topWeight = _mm_set1_epi16(wTop);
		const __m128i bottomWeight = _mm_set1_epi16(wBottom);
		for (; i + 4 <= count; i += 4) {
			const __m128i a = _mm_loadu_si128((const __m128i*)(top + i));
			const __m128i b = _mm_loadu_si128((const __m128i*)(bottom + i));
			__m128i low = _mm_add_epi16(
				_mm_mullo_epi16(_mm_unpacklo_epi8(a, zero), topWeight),
				_mm_mullo_epi16(_mm_unpacklo_epi8(b, zero), bottomWeight));
			__m128i high = _mm_add_epi16(
				_mm_mullo_epi16(_mm_unpackhi_epi8(a, zero), topWeight),
				_mm_mullo_epi16(_mm_unpackhi_epi8(b, zero), bottomWeight));
			_mm_storeu_si128((__m128i*)(fRow + i), _mm_packus_epi16(
				_mm_srli_epi16(low, 8), _mm_srli_epi16(high, 8)));
		}
#endif
		for (; i < count; i++)
			fRow[i] = _Blend(top[i], bottom[i], wTop, wBottom);
	}

	void _BlendColumns(uint32* destination, int32 width)
	{
		int32 x = 0;
#ifdef __SSE2__
		const __m128i zero = _mm_setzero_si128();
		for (; x + 4 <= width; x += 4) {
			__m128i sums[4];
			for (int32 i = 0; i < 4; i++) {
				const Column& column = fColumns[x + i];
				// a pixel and its right neighbour, channel by channel:
				// left blue, right blue, left green, ... as 16 bit
				__m128i pair = _mm_loadl_epi64(
					(const __m128i*)(fRow + column.offset));
				pair = _mm_unpacklo_epi8(
					_mm_unpacklo_epi8(pair, _mm_srli_si128(pair, 4)), zero);
				sums[i] = _mm_srli_epi32(_mm_madd_epi16(pair,
					_mm_set1_epi32(column.weights)), 8);
			}
			_mm_storeu_si128((__m128i*)(destination + x), _mm_packus_epi16(
				_mm_packs_epi32(sums[0], sums[1]),
				_mm_packs_epi32(sums[2], sums[3])));
		}
#endif
		for (; x < width; x++) {
			const Column& column = fColumns[x];
			const uint32* s = fRow + column.offset;
			destination[x] = _Blend(s[0], s[1], column.weights & 0xffff,
				column.weights >> 16);
		}
	}

	static inline uint32 _Premultiplied(uint32 color)
	{
		const uint32 alpha = color >> 24;
		if (alpha == 255)
			return color;
		uint32 blueRed = (color & 0x00ff00ff) * alpha + 0x00800080;
		blueRed = ((blueRed + ((blueRed >> 8) & 0x00ff00ff)) >> 8) & 0x00ff00ff;
		uint32 green = ((color >> 8) & 0xff) * alpha + 0x80;
		green = (green + (green >> 8)) >> 8;
		return alpha << 24 | green << 8 | blueRed;
	}

	void _BlendColumnsOver(uint32* destination, int32 width)
	{
		for (int32 x = 0; x < width; x++) {
			const Column& column = fColumns[x];
			const uint32* s = fRow + column.offset;
			const uint32 color = _Blend(s[0], s[1], column.weights & 0xffff,
				column.weights >> 16);
			const uint32 alpha = color >> 24;
			if (alpha == 0)
				continue;
			if (alpha == 255) {
				destination[x] = (destination[x] & 0xff000000)
					| (color & 0x00ffffff);
				continue;
			}
			// the colour is premultiplied: over the destination, which
			// keeps its own alpha as B_OP_ALPHA leaves it
			const uint32 rest = 255 - alpha;
			uint8* d = (uint8*)&destination[x];
			for (int32 i = 0; i < 3; i++) {
				const uint32 value = ((color >> (8 * i)) & 0xff)
					+ (d[i] * rest + 127) / 255;
				d[i] = value > 255 ? 255 : value;
			}
		}
	}

	bool _Reserve(int32 rowSize, int32 columns)
	{
		if (rowSize > fRowSize) {
			uint32* row = (uint32*)realloc(fRow, rowSize * 4);
			if (row == NULL)
				return false;
			fRow = row;
			fRowSize = rowSize;
		}
		if (columns > fColumnsSize) {
			Column* list = (Column*)realloc(fColumns,
				columns * sizeof(Column));
			if (list == NULL)
				return false;
			fColumns = list;
			fColumnsSize = columns;
		}
		return true;
	}

	uint32*	fRow;
	int32	fRowSize;
	Column*	fColumns;
	int32	fColumnsSize;
	bool	fOverlay;
};


#ifdef __i386__

struct BilinearSimd : DrawBitmapBilinearOptimized<BilinearSimd> {
	void DrawToClipRect(int32 xIndexL, int32 xIndexR, int32 y1, int32 y2)
	{
		// Basically the same as the "standard" mode, but we use SIMD
		// routines for the processing of the single display lines.

		// The last column/row handling does not need to be performed
		// for all clipping rects!
		int32 yMax = y2;
		if (fWeightsY[yMax].weight == 255)
			yMax--;
		int32 xIndexMax = xIndexR;
		if (fWeightsX[xIndexMax].weight == 255)
			xIndexMax--;

		for (; y1 <= yMax; y1++) {
			// cache the weight of the top and bottom row
			const uint16 wTop = fWeightsY[y1].weight;
			const uint16 wBottom = 255 - fWeightsY[y1].weight;

			// buffer offset into source (top row)
			const uint8* src = fSource->row_ptr(fWeightsY[y1].index);
			// buffer handle for destination to be incremented per
			// pixel
			uint8* d = fDestination;
			bilinear_scale_xloop_mmxsse(src, fDestination, fWeightsX, xIndexL,
				xIndexMax, wTop, fSourceBytesPerRow);
			// increase pointer by processed pixels
			d += (xIndexMax - xIndexL + 1) * 4;

			// last column of pixels if necessary
			if (xIndexMax < xIndexR) {
				const uint8* s = src + fWeightsX[xIndexR].index;
				const uint8* sBottom = s + fSourceBytesPerRow;
				d[0] = (s[0] * wTop + sBottom[0] * wBottom) >> 8;
				d[1] = (s[1] * wTop + sBottom[1] * wBottom) >> 8;
				d[2] = (s[2] * wTop + sBottom[2] * wBottom) >> 8;
			}

			fDestination += fDestinationBytesPerRow;
		}

		// last row of pixels if necessary
		// buffer offset into source (bottom row)
		const uint8* src = fSource->row_ptr(fWeightsY[y2].index);
		// buffer handle for destination to be incremented per pixel
		uint8* d = fDestination;

		if (yMax < y2) {
			for (int32 x = xIndexL; x <= xIndexMax; x++) {
				const uint8* s = src + fWeightsX[x].index;
				const uint16 wLeft = fWeightsX[x].weight;
				const uint16 wRight = 255 - wLeft;
				d[0] = (s[0] * wLeft + s[4] * wRight) >> 8;
				d[1] = (s[1] * wLeft + s[5] * wRight) >> 8;
				d[2] = (s[2] * wLeft + s[6] * wRight) >> 8;
				d += 4;
			}
		}

		// pixel in bottom right corner if necessary
		if (yMax < y2 && xIndexMax < xIndexR) {
			const uint8* s = src + fWeightsX[xIndexR].index;
			*(uint32*)d = *(uint32*)s;
		}
	}
};

#endif	// __i386__


template<class ColorType, class DrawMode>
struct DrawBitmapBilinear {
	void
	Draw(const Painter* painter, PainterAggInterface& aggInterface,
		agg::rendering_buffer& bitmap, BPoint offset,
		double scaleX, double scaleY, BRect destinationRect)
	{
		//bigtime_t now = system_time();
		uint32 dstWidth = destinationRect.IntegerWidth() + 1;
		uint32 dstHeight = destinationRect.IntegerHeight() + 1;
		uint32 srcWidth = bitmap.width();
		uint32 srcHeight = bitmap.height();

		// Do not calculate more filter weights than necessary and also
		// keep the stack based allocations reasonably sized
		const BRegion& clippingRegion = *painter->ClippingRegion();
		if (clippingRegion.Frame().IntegerWidth() + 1 < (int32)dstWidth)
			dstWidth = clippingRegion.Frame().IntegerWidth() + 1;
		if (clippingRegion.Frame().IntegerHeight() + 1 < (int32)dstHeight)
			dstHeight = clippingRegion.Frame().IntegerHeight() + 1;

		// When calculating less filter weights than specified by
		// destinationRect, we need to compensate the offset.
		FilterData filterData;
		filterData.fIndexOffsetX = 0;
		filterData.fIndexOffsetY = 0;
		if (clippingRegion.Frame().left > destinationRect.left) {
			filterData.fIndexOffsetX = (int32)(clippingRegion.Frame().left
				- destinationRect.left);
		}
		if (clippingRegion.Frame().top > destinationRect.top) {
			filterData.fIndexOffsetY = (int32)(clippingRegion.Frame().top
				- destinationRect.top);
		}

//#define FILTER_INFOS_ON_HEAP
#ifdef FILTER_INFOS_ON_HEAP
		filterData.fWeightsX = new (nothrow) FilterInfo[dstWidth];
		filterData.fWeightsY = new (nothrow) FilterInfo[dstHeight];
		if (filterData.fWeightsX == NULL || filterData.fWeightsY == NULL) {
			delete[] filterData.fWeightsX;
			delete[] filterData.fWeightsY;
			return;
		}
#else
		// stack based saves about 200µs on 1.85 GHz Core 2 Duo
		// should not pose a problem with stack overflows
		// (needs around 12Kb for 1920x1200)
		FilterInfo xWeights[dstWidth];
		FilterInfo yWeights[dstHeight];
		filterData.fWeightsX = &xWeights[0];
		filterData.fWeightsY = &yWeights[0];
#endif

		// Extract the cropping information for the source bitmap,
		// If only a part of the source bitmap is to be drawn with scale,
		// the offset will be different from the destinationRect left top
		// corner.
		const int32 xBitmapShift = (int32)(destinationRect.left - offset.x);
		const int32 yBitmapShift = (int32)(destinationRect.top - offset.y);

		for (uint32 i = 0; i < dstWidth; i++) {
			// fractional index into source
			// NOTE: It is very important to calculate the fractional index
			// into the source pixel grid like this to prevent out of bounds
			// access! It will result in the rightmost pixel of the destination
			// to access the rightmost pixel of the source with a weighting
			// of 255. This in turn will trigger an optimization in the loop
			// that also prevents out of bounds access.
			float index = (i + filterData.fIndexOffsetX) * (srcWidth - 1)
				/ (srcWidth * scaleX - 1);
			// round down to get the left pixel
			filterData.fWeightsX[i].index = (uint16)index;
			filterData.fWeightsX[i].weight =
				255 - (uint16)((index - filterData.fWeightsX[i].index) * 255);
			// handle cropped source bitmap
			filterData.fWeightsX[i].index += xBitmapShift;
			// precompute index for 32 bit pixels
			filterData.fWeightsX[i].index *= 4;
		}

		for (uint32 i = 0; i < dstHeight; i++) {
			// fractional index into source
			// NOTE: It is very important to calculate the fractional index
			// into the source pixel grid like this to prevent out of bounds
			// access! It will result in the bottommost pixel of the
			// destination to access the bottommost pixel of the source with a
			// weighting of 255. This in turn will trigger an optimization in
			// the loop that also prevents out of bounds access.
			float index = (i + filterData.fIndexOffsetY) * (srcHeight - 1)
				/ (srcHeight * scaleY - 1);
			// round down to get the top pixel
			filterData.fWeightsY[i].index = (uint16)index;
			filterData.fWeightsY[i].weight =
				255 - (uint16)((index - filterData.fWeightsY[i].index) * 255);
			// handle cropped source bitmap
			filterData.fWeightsY[i].index += yBitmapShift;
		}
		//printf("X: %d/%d ... %d/%d, %d/%d (%ld)\n",
		//	xWeights[0].index, xWeights[0].weight,
		//	xWeights[dstWidth - 2].index, xWeights[dstWidth - 2].weight,
		//	xWeights[dstWidth - 1].index, xWeights[dstWidth - 1].weight,
		//	dstWidth);
		//printf("Y: %d/%d ... %d/%d, %d/%d (%ld)\n",
		//	yWeights[0].index, yWeights[0].weight,
		//	yWeights[dstHeight - 2].index, yWeights[dstHeight - 2].weight,
		//	yWeights[dstHeight - 1].index, yWeights[dstHeight - 1].weight,
		//	dstHeight);

		// Figure out which version of the code we want to use...
		enum {
			kOptimizeForLowFilterRatio = 0,
			kUseDefaultVersion,
			kUseSIMDVersion,
			kUseSeparableVersion
		};

		int codeSelect = kUseDefaultVersion;

		if (typeid(ColorType) == typeid(ColorTypeRgb)
			&& typeid(DrawMode) == typeid(DrawModeCopy)) {
			codeSelect = kUseSeparableVersion;
#ifdef __i386__
			uint32 neededSIMDFlags = APPSERVER_SIMD_MMX | APPSERVER_SIMD_SSE;
			if ((gSIMDFlags & neededSIMDFlags) == neededSIMDFlags)
				codeSelect = kUseSIMDVersion;
#endif
		}

		switch (codeSelect) {
			case kUseDefaultVersion:
			{
				BilinearDefault<ColorType, DrawMode> bilinearPainter;
				bilinearPainter.Draw(aggInterface, destinationRect, &bitmap,
					filterData);
				break;
			}

			case kOptimizeForLowFilterRatio:
			{
				BilinearLowFilterRatio bilinearPainter;
				bilinearPainter.Draw(aggInterface, destinationRect,
					&bitmap, filterData);
				break;
			}

			case kUseSeparableVersion:
			{
				BilinearSeparable bilinearPainter;
				bilinearPainter.Draw(aggInterface, destinationRect,
					&bitmap, filterData);
				break;
			}

#ifdef __i386__
			case kUseSIMDVersion:
			{
				BilinearSimd bilinearPainter;
				bilinearPainter.Draw(aggInterface, destinationRect, &bitmap,
					filterData);
				break;
			}
#endif	// __i386__
		}

#ifdef FILTER_INFOS_ON_HEAP
		delete[] filterData.fWeightsX;
		delete[] filterData.fWeightsY;
#endif
		//printf("draw bitmap %.5fx%.5f: %lld\n", scaleX, scaleY,
		//	system_time() - now);
	}
};


} // namespace BitmapPainterPrivate


#endif // DRAW_BITMAP_BILINEAR_H
