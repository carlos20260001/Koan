/*
 * Copyright 2026, air/OS contributors.
 * Distributed under the terms of the MIT License.
 */
#ifndef _FRAME_LINES_H
#define _FRAME_LINES_H


#include <ControlLook.h>
#include <ServerProtocolStructs.h>
#include <View.h>
#include <ViewPrivate.h>


namespace BPrivate {


/*!	Collects the one pixel lines of the rings of a frame, nested inwards
	from the rectangle it starts with, and draws them in one go. At a
	fractional scale one logical pixel is one frame buffer pixel in some
	places and two in others, so a ring drawn alone is as thin as one on one
	side of a control and as thick as two on the other; drawn together,
	app_server shares out the pixels of the band the rings cover the same
	way on every side (see ViewFrameLineArrayInfo). The dark outline of a
	control should be a ring that keeps its width; a faint edge around it
	can take up what is left.

	\a borders (BControlLook::B_LEFT_BORDER and so on) says on which sides
	of the frame the rings are; lines elsewhere stay on the usual grid. A
	single line along one side of a rectangle, such as a separator, is a
	frame with one ring on that side.
*/
class FrameLines {
public:
	FrameLines(BView* view, const BRect& frame, uint32 borders)
		:
		fView(view),
		fFrame(frame),
		fBorders(borders),
		fRings(0),
		fUniformRings(0),
		fFlags(0),
		fCount(0)
	{
	}

	//! The rings are drawn over a fill: they keep their widths and the
	//! fill takes up what the scale leaves.
	void SetOverFill()
	{
		fFlags |= kFrameLinesOverFill;
	}

	//! Over a fill that does not reach under every ring (a separator in a
	//! transparent view): every ring also covers the pixels of its own
	//! logical pixel, at the cost of an uneven width at scales such as 125
	//! percent, where the rounded scale is the smaller width.
	void SetOverFillCoveringPixels()
	{
		fFlags |= kFrameLinesOverFill | kFrameLinesCoverPixels;
	}

	void AddLine(BPoint start, BPoint end, const rgb_color& color)
	{
		if (fCount == kMaxLines)
			return;
		fLines[fCount].startPoint = start;
		fLines[fCount].endPoint = end;
		fLines[fCount].color = color;
		fCount++;
	}

	void EndRing(bool uniform)
	{
		if (uniform)
			fUniformRings |= 1 << fRings;
		fRings++;
	}

	void Draw()
	{
		if (fCount == 0)
			return;
		BView::Private(fView).StrokeFrameLineArray(fFrame, fRings, fBorders,
			fUniformRings, fFlags, fLines, fCount);
		fCount = 0;
	}

private:
	static const int32 kMaxLines = 48;

	BView*				fView;
	BRect				fFrame;
	uint32				fBorders;
	int32				fRings;
	uint32				fUniformRings;
	uint32				fFlags;
	ViewLineArrayInfo	fLines[kMaxLines];
	int32				fCount;
};


/*!	A one pixel line along the \a side (BControlLook::B_LEFT_BORDER and so
	on) of \a rect, drawn over what is inside \a rect: at a fractional scale
	it has the scale's rounded width wherever it falls (2 pixels at 150 and
	175 percent), and never less than its logical pixel. For separators and
	edges drawn over a background.
*/
static inline void
stroke_line_over_fill(BView* view, const BRect& rect, uint32 side,
	const rgb_color& color)
{
	BPoint start;
	BPoint end;
	switch (side) {
		case BControlLook::B_LEFT_BORDER:
			start = rect.LeftTop();
			end = rect.LeftBottom();
			break;
		case BControlLook::B_TOP_BORDER:
			start = rect.LeftTop();
			end = rect.RightTop();
			break;
		case BControlLook::B_RIGHT_BORDER:
			start = rect.RightTop();
			end = rect.RightBottom();
			break;
		default:
			side = BControlLook::B_BOTTOM_BORDER;
			start = rect.LeftBottom();
			end = rect.RightBottom();
			break;
	}

	FrameLines line(view, rect, side);
	line.SetOverFillCoveringPixels();
	line.AddLine(start, end, color);
	line.EndRing(true);
	line.Draw();
}


/*!	A flag for BControlLook::DrawBorder() that HaikuControlLook knows (other
	control looks ignore it): the border is drawn over what is inside it,
	see FrameLines::SetOverFillCoveringPixels(). A view that draws its
	border before what is inside it draws it again with this flag after.
*/
static const uint32 kControlLookBorderOverFill = 1 << 15;


}	// namespace BPrivate


using BPrivate::FrameLines;
using BPrivate::kControlLookBorderOverFill;
using BPrivate::stroke_line_over_fill;


#endif	// _FRAME_LINES_H
