/*
 * Copyright 2009, Haiku. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 *		Stephan Aßmus, <superstippi@gmx.de>
 */
#ifndef APP_SERVER_PROTOCOL_STRUCTS_H
#define APP_SERVER_PROTOCOL_STRUCTS_H


#include <AffineTransform.h>
#include <Rect.h>


struct ViewSetStateInfo {
	BPoint						penLocation;
	float						penSize;
	rgb_color					highColor;
	rgb_color					lowColor;
	color_which					whichHighColor;
	color_which					whichLowColor;
	float						whichHighColorTint;
	float						whichLowColorTint;
	::pattern					pattern;
	drawing_mode				drawingMode;
	BPoint						origin;
	float						scale;
	join_mode					lineJoin;
	cap_mode					lineCap;
	float						miterLimit;
	int32						fillRule;
	source_alpha				alphaSourceMode;
	alpha_function				alphaFunctionMode;
	bool						fontAntialiasing;
};


struct ViewGetStateInfo {
	int32						fontID;
	float						fontSize;
	float						fontShear;
	float						fontRotation;
	float						fontFalseBoldWidth;
	int8						fontSpacing;
	int8						fontEncoding;
	int16						fontFace;
	int32						fontFlags;

	ViewSetStateInfo			viewStateInfo;
};


struct ViewDragImageInfo {
	int32						bitmapToken;
	int32						dragMode;
	BPoint						offset;
	int32						bufferSize;
};


struct ViewSetViewCursorInfo {
	int32						cursorToken;
	int32						viewToken;
	bool						sync;
};


struct ViewBeginRectTrackingInfo {
	BRect						rect;
	uint32						style;
};


struct ViewSetLineModeInfo {
	join_mode					lineJoin;
	cap_mode					lineCap;
	float						miterLimit;
};


struct ViewBlendingModeInfo {
	source_alpha				sourceAlpha;
	alpha_function				alphaFunction;
};


struct ViewDrawBitmapInfo {
	int32						bitmapToken;
	uint32						options;
	BRect						viewRect;
	BRect						bitmapRect;
};


struct ViewDrawStringInfo {
	int32						stringLength;
	BPoint						location;
	escapement_delta			delta;
};


struct ViewStrokeLineInfo {
	BPoint						startPoint;
	BPoint						endPoint;
};


struct ViewLineArrayInfo {
	BPoint						startPoint;
	BPoint						endPoint;
	rgb_color					color;
};


// Precedes the lines of AS_STROKE_FRAME_LINEARRAY: one pixel lines that draw
// the rings of a frame, nested from \a frame inwards on the sides in
// \a borders, \a rings of them. At a fractional scale app_server gives
// every side of the band they cover its own pixels on the frame buffer's
// grid, its outer and inner edges where any other drawing puts them, and
// shares them out between the rings: those in \a uniformRings (bit i for
// ring i, the outermost being 0) keep the same width on every side, the
// others take what is left. Elsewhere the lines are drawn as they are.
struct ViewFrameLineArrayInfo {
	BRect						frame;
	int32						rings;
	uint32						borders;
	uint32						uniformRings;
	uint32						flags;
	int32						lineCount;
};

enum {
	// The rings are drawn over a fill that reaches under them: every ring
	// gets its width from the outside in, and the band ends where they do.
	kFrameLinesOverFill			= 0x01,
	// With kFrameLinesOverFill: every ring also covers at least the pixels
	// of the logical pixel it is drawn on, for lines over something that
	// does not reach under them everywhere (a transparent view).
	kFrameLinesCoverPixels		= 0x02
};


#endif	// APP_SERVER_PROTOCOL_STRUCTS_H
