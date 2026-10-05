/*
 * Copyright 2026, Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef SCALED_READBACK_H
#define SCALED_READBACK_H


#include <SupportDefs.h>


// A screenshot of a screen drawn at a higher density than its logical size
// (DrawingEngine::ReadBitmap): each logical pixel is the average of the frame
// buffer pixels it covers, as the Painter maps them. Nothing but the pixels
// asked for is read, and nothing is copied on the way: this runs with the
// drawing engine locked, and every window waits for it.

struct ScaledReadbackPixels {
	const uint8*	bits;
		// 32 bits per pixel (B_RGB32 or B_RGBA32)
	uint32			bytesPerRow;
};

struct ScaledReadbackCursor {
	const uint8*	bits;
		// B_RGBA32, premultiplied, as ServerCursor keeps it
	int32			width;
	int32			height;
	int32			left;
	int32			top;
		// frame buffer position of its top left pixel
};

// Writes the logical pixels from (left, top), width by height of them, into
// destination (32 bits per pixel, opaque). Frame buffer pixels are read only
// inside deviceLeft..deviceRight and deviceTop..deviceBottom (inclusive, in
// the buffer). With a cursor, the pixels it covers are averaged with it
// blended in at frame buffer density, as it appears on screen.
status_t scaled_readback(const ScaledReadbackPixels& source, float scale,
	float left, float top, int32 width, int32 height,
	int32 deviceLeft, int32 deviceTop, int32 deviceRight, int32 deviceBottom,
	const ScaledReadbackCursor* cursor, uint8* destination,
	uint32 destinationBytesPerRow);


#endif	// SCALED_READBACK_H
