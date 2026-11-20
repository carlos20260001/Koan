/*
 * Copyright 2026, Koan contributors. All rights reserved.
 * Distributed under the terms of the MIT License.
 */


#include "KmsBlit.h"

#include <alloca.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>


static const int32 kWeightShift = 14;
static const int32 kWeightOne = 1 << kWeightShift;


void
kms_copy_rect(const uint8* source, uint32 sourceBytesPerRow,
	uint8* destination, uint32 destinationBytesPerRow, const KmsRect& area,
	int32 destinationX, int32 destinationY, bool opaque)
{
	if (area.IsEmpty())
		return;
	const uint8* from = source + (size_t)area.y * sourceBytesPerRow
		+ (size_t)area.x * 4;
	uint8* to = destination + (size_t)destinationY * destinationBytesPerRow
		+ (size_t)destinationX * 4;
	size_t bytes = (size_t)area.width * 4;
	for (int32 y = 0; y < area.height; y++) {
		memcpy(to, from, bytes);
		if (opaque) {
			// app_server's buffers leave alpha as it comes; scan-out is X
			uint32* pixel = (uint32*)to;
			for (int32 x = 0; x < area.width; x++)
				pixel[x] |= 0xff000000;
		}
		from += sourceBytesPerRow;
		to += destinationBytesPerRow;
	}
}


void
kms_fill_rect(uint8* destination, uint32 bytesPerRow, const KmsRect& area,
	uint32 color)
{
	for (int32 y = 0; y < area.height; y++) {
		uint32* row = (uint32*)(destination + (size_t)(area.y + y) * bytesPerRow)
			+ area.x;
		for (int32 x = 0; x < area.width; x++)
			row[x] = color;
	}
}


// #pragma mark - KmsScaler


KmsScaler::KmsScaler()
	:
	fSourceWidth(0),
	fSourceHeight(0),
	fDestinationWidth(0),
	fDestinationHeight(0)
{
	memset(&fHorizontal, 0, sizeof(fHorizontal));
	memset(&fVertical, 0, sizeof(fVertical));
}


KmsScaler::~KmsScaler()
{
	_FreeAxis(fHorizontal);
	_FreeAxis(fVertical);
}


void
KmsScaler::_FreeAxis(Axis& axis)
{
	free(axis.first);
	free(axis.count);
	free(axis.weights);
	free(axis.offsets);
	memset(&axis, 0, sizeof(axis));
}


status_t
KmsScaler::_BuildAxis(Axis& axis, int32 sourceLength,
	int32 destinationLength)
{
	_FreeAxis(axis);
	double ratio = (double)sourceLength / destinationLength;
	double support = ratio > 1.0 ? ratio : 1.0;
	int32 maxTaps = (int32)ceil(support) * 2 + 1;
	axis.first = (int32*)malloc(sizeof(int32) * destinationLength);
	axis.count = (int32*)malloc(sizeof(int32) * destinationLength);
	axis.offsets = (int32*)malloc(sizeof(int32) * destinationLength);
	axis.weights = (int32*)malloc(sizeof(int32) * destinationLength
		* maxTaps);
	if (axis.first == NULL || axis.count == NULL || axis.offsets == NULL
		|| axis.weights == NULL) {
		_FreeAxis(axis);
		return B_NO_MEMORY;
	}
	axis.maxTaps = maxTaps;

	double* raw = (double*)alloca(sizeof(double) * maxTaps);
	int32 offset = 0;
	for (int32 i = 0; i < destinationLength; i++) {
		axis.offsets[i] = offset;
		if (sourceLength == destinationLength) {
			axis.first[i] = i;
			axis.count[i] = 1;
			axis.weights[offset++] = kWeightOne;
			continue;
		}
		double center = (i + 0.5) * ratio - 0.5;
		int32 from = (int32)ceil(center - support);
		int32 to = (int32)floor(center + support);
		// clamp to the edge: weights outside go to the edge pixel
		int32 first = from < 0 ? 0 : from;
		int32 last = to > sourceLength - 1 ? sourceLength - 1 : to;
		if (last < first)
			last = first;
		int32 count = last - first + 1;
		if (count > maxTaps)
			count = maxTaps;
		for (int32 t = 0; t < count; t++)
			raw[t] = 0;
		double total = 0;
		for (int32 j = from; j <= to; j++) {
			double weight = 1.0 - fabs(j - center) / support;
			if (weight <= 0)
				continue;
			int32 k = j < first ? first : j > last ? last : j;
			int32 tap = k - first;
			if (tap >= count)
				tap = count - 1;
			raw[tap] += weight;
			total += weight;
		}
		if (total <= 0) {
			raw[0] = 1;
			total = 1;
		}
		// to fixed point, the rounding remainder on the largest tap
		int32 sum = 0;
		int32 largest = 0;
		for (int32 t = 0; t < count; t++) {
			int32 weight = (int32)floor(raw[t] / total * kWeightOne + 0.5);
			axis.weights[offset + t] = weight;
			sum += weight;
			if (weight > axis.weights[offset + largest])
				largest = t;
		}
		axis.weights[offset + largest] += kWeightOne - sum;
		axis.first[i] = first;
		axis.count[i] = count;
		offset += count;
	}
	return B_OK;
}


status_t
KmsScaler::SetTo(int32 sourceWidth, int32 sourceHeight,
	int32 destinationWidth, int32 destinationHeight)
{
	if (sourceWidth <= 0 || sourceHeight <= 0 || destinationWidth <= 0
		|| destinationHeight <= 0)
		return B_BAD_VALUE;
	if (sourceWidth == fSourceWidth && sourceHeight == fSourceHeight
		&& destinationWidth == fDestinationWidth
		&& destinationHeight == fDestinationHeight)
		return B_OK;
	fSourceWidth = fSourceHeight = fDestinationWidth = fDestinationHeight = 0;
	status_t status = _BuildAxis(fHorizontal, sourceWidth, destinationWidth);
	if (status == B_OK)
		status = _BuildAxis(fVertical, sourceHeight, destinationHeight);
	if (status != B_OK)
		return status;
	fSourceWidth = sourceWidth;
	fSourceHeight = sourceHeight;
	fDestinationWidth = destinationWidth;
	fDestinationHeight = destinationHeight;
	return B_OK;
}


bool
KmsScaler::IsIdentity() const
{
	return fSourceWidth == fDestinationWidth
		&& fSourceHeight == fDestinationHeight;
}


void
KmsScaler::Scale(const uint8* source, uint32 sourceBytesPerRow,
	uint8* destination, uint32 destinationBytesPerRow, const KmsRect& _area,
	bool opaque) const
{
	KmsRect area = _area.Intersect(KmsRect(0, 0, fDestinationWidth,
		fDestinationHeight));
	if (area.IsEmpty())
		return;
	if (IsIdentity()) {
		kms_copy_rect(source, sourceBytesPerRow, destination,
			destinationBytesPerRow, area, area.x, area.y, opaque);
		return;
	}

	// the source rows the area's rows read
	int32 rowFirst = fVertical.first[area.y];
	int32 rowLast = rowFirst;
	for (int32 y = area.y; y < area.Bottom(); y++) {
		int32 last = fVertical.first[y] + fVertical.count[y] - 1;
		if (fVertical.first[y] < rowFirst)
			rowFirst = fVertical.first[y];
		if (last > rowLast)
			rowLast = last;
	}
	int32 rows = rowLast - rowFirst + 1;

	// horizontal pass: those rows, the area's columns, 8 bits a channel
	uint8* temp = (uint8*)malloc((size_t)rows * area.width * 4);
	if (temp == NULL)
		return;
	for (int32 r = 0; r < rows; r++) {
		const uint8* row = source + (size_t)(rowFirst + r) * sourceBytesPerRow;
		uint8* out = temp + (size_t)r * area.width * 4;
		for (int32 x = 0; x < area.width; x++) {
			int32 i = area.x + x;
			const int32* weights = fHorizontal.weights + fHorizontal.offsets[i];
			const uint8* pixel = row + fHorizontal.first[i] * 4;
			int32 b = 0, g = 0, red = 0, a = 0;
			for (int32 t = 0; t < fHorizontal.count[i]; t++) {
				int32 w = weights[t];
				b += pixel[0] * w;
				g += pixel[1] * w;
				red += pixel[2] * w;
				a += pixel[3] * w;
				pixel += 4;
			}
			out[0] = (b + kWeightOne / 2) >> kWeightShift;
			out[1] = (g + kWeightOne / 2) >> kWeightShift;
			out[2] = (red + kWeightOne / 2) >> kWeightShift;
			out[3] = (a + kWeightOne / 2) >> kWeightShift;
			out += 4;
		}
	}

	// vertical pass into the destination
	for (int32 y = area.y; y < area.Bottom(); y++) {
		const int32* weights = fVertical.weights + fVertical.offsets[y];
		int32 first = fVertical.first[y] - rowFirst;
		uint8* out = destination + (size_t)y * destinationBytesPerRow
			+ (size_t)area.x * 4;
		for (int32 x = 0; x < area.width; x++) {
			int32 b = 0, g = 0, red = 0, a = 0;
			const uint8* pixel = temp + ((size_t)first * area.width + x) * 4;
			for (int32 t = 0; t < fVertical.count[y]; t++) {
				int32 w = weights[t];
				b += pixel[0] * w;
				g += pixel[1] * w;
				red += pixel[2] * w;
				a += pixel[3] * w;
				pixel += (size_t)area.width * 4;
			}
			out[0] = (b + kWeightOne / 2) >> kWeightShift;
			out[1] = (g + kWeightOne / 2) >> kWeightShift;
			out[2] = (red + kWeightOne / 2) >> kWeightShift;
			out[3] = opaque ? 255 : (a + kWeightOne / 2) >> kWeightShift;
			out += 4;
		}
	}
	free(temp);
}
