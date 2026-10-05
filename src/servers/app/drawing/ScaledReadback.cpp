/*
 * Copyright 2026, Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 */


#include "ScaledReadback.h"

#include <math.h>
#include <string.h>

#include <algorithm>
#include <new>

#include <ByteOrder.h>


namespace {


// The frame buffer pixels a logical pixel covers along one axis, inclusive;
// empty (last < first) where nothing may be read.
struct Span {
	int32	first;
	int32	last;
};


#if B_HOST_IS_LENDIAN
const uint32 kOpaque = 0xff000000;
#else
const uint32 kOpaque = 0x000000ff;
#endif


// Logical to buffer coordinates as the Painter maps them, clipped to
// low..high.
void
compute_spans(Span* spans, float start, int32 count, float scale, int32 low,
	int32 high)
{
	for (int32 i = 0; i < count; i++) {
		int32 first = (int32)floorf((start + i) * scale);
		int32 last = (int32)floorf((start + i + 1) * scale) - 1;
		first = std::max(low, first);
		last = std::min(high, std::max(first, last));
		spans[i].first = first;
		spans[i].last = last;
	}
}


// The average of the pixels of `columns` by `rows`, opaque; black where
// there are none.
inline uint32
average(const uint8* bits, uint32 bytesPerRow, const Span& columns,
	const Span& rows)
{
	if (columns.last < columns.first || rows.last < rows.first)
		return kOpaque;

	uint32 sum[3] = { 0, 0, 0 };
	for (int32 y = rows.first; y <= rows.last; y++) {
		const uint8* s = bits + (size_t)y * bytesPerRow + columns.first * 4;
		for (int32 x = columns.first; x <= columns.last; x++, s += 4) {
			sum[0] += s[0];
			sum[1] += s[1];
			sum[2] += s[2];
		}
	}
	uint32 samples = (columns.last - columns.first + 1)
		* (rows.last - rows.first + 1);

	uint32 pixel;
	uint8* d = (uint8*)&pixel;
	d[0] = sum[0] / samples;
	d[1] = sum[1] / samples;
	d[2] = sum[2] / samples;
	d[3] = 255;
	return pixel;
}


// Twice the density: two buffer rows into one logical row, each pair of
// pixels into one. Four pixels' channels add up within the 16-bit lanes of
// a word, so the sums divide by four exactly as average() does, without
// unpacking the bytes.
void
average_2x2(const uint32* row0, const uint32* row1, uint32* destination,
	int32 count)
{
	for (int32 x = 0; x < count; x++) {
		uint32 a = row0[2 * x];
		uint32 b = row0[2 * x + 1];
		uint32 c = row1[2 * x];
		uint32 d = row1[2 * x + 1];
		uint32 even = (a & 0x00ff00ff) + (b & 0x00ff00ff) + (c & 0x00ff00ff)
			+ (d & 0x00ff00ff);
		uint32 odd = ((a >> 8) & 0x00ff00ff) + ((b >> 8) & 0x00ff00ff)
			+ ((c >> 8) & 0x00ff00ff) + ((d >> 8) & 0x00ff00ff);
		destination[x] = ((even >> 2) & 0x00ff00ff)
			| (((odd >> 2) & 0x00ff00ff) << 8) | kOpaque;
	}
}


// The first and last of `spans` that reach into low..high, or -1.
void
touching(const Span* spans, int32 count, int32 low, int32 high, int32& first,
	int32& last)
{
	first = last = -1;
	for (int32 i = 0; i < count; i++) {
		if (spans[i].first <= spans[i].last && spans[i].last >= low
			&& spans[i].first <= high) {
			if (first < 0)
				first = i;
			last = i;
		}
	}
}


}	// namespace


status_t
scaled_readback(const ScaledReadbackPixels& source, float scale, float left,
	float top, int32 width, int32 height, int32 deviceLeft, int32 deviceTop,
	int32 deviceRight, int32 deviceBottom, const ScaledReadbackCursor* cursor,
	uint8* destination, uint32 destinationBytesPerRow)
{
	if (width <= 0 || height <= 0)
		return B_OK;

	Span* columns = new(std::nothrow) Span[width + height];
	if (columns == NULL)
		return B_NO_MEMORY;
	Span* rows = columns + width;
	compute_spans(columns, left, width, scale, deviceLeft, deviceRight);
	compute_spans(rows, top, height, scale, deviceTop, deviceBottom);

	// At twice the density each logical pixel of a row covers the next two
	// buffer columns, up to where the buffer ends.
	int32 paired = 0;
	if (scale == 2) {
		while (paired < width
			&& columns[paired].first == columns[0].first + 2 * paired
			&& columns[paired].last == columns[paired].first + 1) {
			paired++;
		}
	}

	for (int32 y = 0; y < height; y++) {
		uint32* d = (uint32*)(destination + (size_t)y * destinationBytesPerRow);
		int32 x = 0;
		if (paired > 0 && rows[y].last == rows[y].first + 1) {
			const uint32* row0 = (const uint32*)(source.bits
				+ (size_t)rows[y].first * source.bytesPerRow) + columns[0].first;
			const uint32* row1 = (const uint32*)((const uint8*)row0
				+ source.bytesPerRow);
			average_2x2(row0, row1, d, paired);
			x = paired;
		}
		for (; x < width; x++)
			d[x] = average(source.bits, source.bytesPerRow, columns[x], rows[y]);
	}

	status_t status = B_OK;
	if (cursor != NULL && cursor->bits != NULL) {
		// The part of the cursor that is read, and the logical pixels it
		// touches: these are averaged again from a copy of the pixels under
		// them with the cursor drawn in.
		int32 cursorLeft = std::max(cursor->left, deviceLeft);
		int32 cursorTop = std::max(cursor->top, deviceTop);
		int32 cursorRight = std::min(cursor->left + cursor->width - 1,
			deviceRight);
		int32 cursorBottom = std::min(cursor->top + cursor->height - 1,
			deviceBottom);
		int32 x0, x1, y0, y1;
		touching(columns, width, cursorLeft, cursorRight, x0, x1);
		touching(rows, height, cursorTop, cursorBottom, y0, y1);
		if (cursorLeft <= cursorRight && cursorTop <= cursorBottom && x0 >= 0
			&& y0 >= 0) {
			int32 patchLeft = columns[x0].first;
			int32 patchTop = rows[y0].first;
			int32 patchWidth = columns[x1].last - patchLeft + 1;
			int32 patchHeight = rows[y1].last - patchTop + 1;
			uint32 patchBytesPerRow = patchWidth * 4;
			uint8* patch = new(std::nothrow) uint8[patchBytesPerRow
				* patchHeight];
			if (patch == NULL)
				status = B_NO_MEMORY;
			else {
				for (int32 y = 0; y < patchHeight; y++) {
					memcpy(patch + y * patchBytesPerRow, source.bits
						+ (size_t)(patchTop + y) * source.bytesPerRow
						+ patchLeft * 4, patchBytesPerRow);
				}

				const uint8* cursorBits = cursor->bits;
				for (int32 y = 0; y < cursor->height; y++) {
					int32 dy = cursor->top + y;
					for (int32 x = 0; x < cursor->width; x++, cursorBits += 4) {
						int32 dx = cursor->left + x;
						if (dx < cursorLeft || dx > cursorRight
							|| dy < cursorTop || dy > cursorBottom
							|| dx < patchLeft || dx >= patchLeft + patchWidth
							|| dy < patchTop || dy >= patchTop + patchHeight)
							continue;
						uint8* p = patch + (dy - patchTop) * patchBytesPerRow
							+ (dx - patchLeft) * 4;
						uint8 alpha = 255 - cursorBits[3];
						p[0] = ((p[0] * alpha) >> 8) + cursorBits[0];
						p[1] = ((p[1] * alpha) >> 8) + cursorBits[1];
						p[2] = ((p[2] * alpha) >> 8) + cursorBits[2];
					}
				}

				for (int32 y = y0; y <= y1; y++) {
					uint32* d = (uint32*)(destination
						+ (size_t)y * destinationBytesPerRow);
					Span patchRows = { rows[y].first - patchTop,
						rows[y].last - patchTop };
					for (int32 x = x0; x <= x1; x++) {
						Span patchColumns = { columns[x].first - patchLeft,
							columns[x].last - patchLeft };
						d[x] = average(patch, patchBytesPerRow, patchColumns,
							patchRows);
					}
				}
				delete[] patch;
			}
		}
	}

	delete[] columns;
	return status;
}
