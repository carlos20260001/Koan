/*
 * Copyright 2001-2018, Haiku, Inc.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 *		Stephan Aßmus <superstippi@gmx.de>
 *		Julian Harnath <julian.harnath@rwth-aachen.de>
 */


#include "DrawingEngine.h"

#include <Bitmap.h>
#include <ControlLook.h>
#include <StackOrHeapArray.h>

#include <stdio.h>

#include <algorithm>
#include <stack>

#include "DrawState.h"
#include "GlyphLayoutEngine.h"
#include "Painter.h"
#include "ServerBitmap.h"
#include "ServerCursor.h"
#include "RenderingBuffer.h"
#include "ScaledReadback.h"

#include "drawing_support.h"


#if DEBUG
#	define ASSERT_PARALLEL_LOCKED() \
	{ if (!IsParallelAccessLocked()) debugger("not parallel locked!"); }
#	define ASSERT_EXCLUSIVE_LOCKED() \
	{ if (!IsExclusiveAccessLocked()) debugger("not exclusive locked!"); }
#else
#	define ASSERT_PARALLEL_LOCKED()
#	define ASSERT_EXCLUSIVE_LOCKED()
#endif


static inline void
make_rect_valid(BRect& rect)
{
	if (rect.left > rect.right) {
		float temp = rect.left;
		rect.left = rect.right;
		rect.right = temp;
	}
	if (rect.top > rect.bottom) {
		float temp = rect.top;
		rect.top = rect.bottom;
		rect.bottom = temp;
	}
}


static inline void
extend_by_stroke_width(BRect& rect, float penSize)
{
	// "- 0.5" because if stroke width == 1, we don't need to extend
	float inset = -ceilf(penSize / 2.0 - 0.5);
	rect.InsetBy(inset, inset);
}


class AutoFloatingOverlaysHider {
	public:
		AutoFloatingOverlaysHider(HWInterface* interface, const BRect& area)
			:
			fInterface(interface),
			fHidden(interface->HideFloatingOverlays(area))
		{
		}

		AutoFloatingOverlaysHider(HWInterface* interface)
			:
			fInterface(interface),
			fHidden(fInterface->HideFloatingOverlays())
		{
		}

		~AutoFloatingOverlaysHider()
		{
			if (fHidden)
				fInterface->ShowFloatingOverlays();
		}

		bool WasHidden() const
		{
			return fHidden;
		}

	private:
		HWInterface*	fInterface;
		bool			fHidden;

};

class DrawTransaction {
public:
	// bounds and regions given here are in buffer pixels
	DrawTransaction(DrawingEngine *engine, const BRect &bounds)
		:
		fEngine(engine),
		fOverlaysHidden(false)
	{
		fDirty.Set(bounds);
		fDirty.IntersectWith(fEngine->fPainter->ClippingRegion());
		if (fDirty.CountRects() == 0)
			return;
		fOverlaysHidden
			= fEngine->fGraphicsCard->HideFloatingOverlays(fDirty.Frame());
	}

	DrawTransaction(DrawingEngine *engine)
		:
		fEngine(engine),
		fOverlaysHidden(false)
	{
		fDirty = *fEngine->fPainter->ClippingRegion();
		if (fDirty.CountRects() == 0)
			return;
		fOverlaysHidden
			= fEngine->fGraphicsCard->HideFloatingOverlays(fDirty.Frame());
	}

	DrawTransaction(DrawingEngine *engine, const BRegion &region)
		:
		fEngine(engine),
		fOverlaysHidden(false)
	{
		// region is already clipped
		fDirty = region;
		if (fDirty.CountRects() == 0)
			return;
		fOverlaysHidden
			= fEngine->fGraphicsCard->HideFloatingOverlays(fDirty.Frame());
	}

	~DrawTransaction()
	{
		if (fEngine->fCopyToFront)
			fEngine->fGraphicsCard->InvalidateRegion(fDirty);
		if (fOverlaysHidden)
			fEngine->fGraphicsCard->ShowFloatingOverlays();
	}

	bool IsDirty() const
	{
		return fDirty.CountRects() > 0;
	}

	void SetDirty(const BRect &rect)
	{
		fDirty.Set(rect);
		fDirty.IntersectWith(fEngine->fPainter->ClippingRegion());
	}

	const BRegion &DirtyRegion() const
	{
		return fDirty;
	}

	bool WasOverlaysHidden() const
	{
		return fOverlaysHidden;
	}

private:
	DrawingEngine *fEngine;
	bool fOverlaysHidden;
	BRegion fDirty;
};


//	#pragma mark -


DrawingEngine::DrawingEngine(HWInterface* interface)
	:
	fPainter(new Painter()),
	fGraphicsCard(NULL),
	fCopyToFront(true)
{
	SetHWInterface(interface);
}


DrawingEngine::~DrawingEngine()
{
	SetHWInterface(NULL);
}


// #pragma mark - locking


bool
DrawingEngine::LockParallelAccess()
{
	return fGraphicsCard->LockParallelAccess();
}


#if DEBUG
bool
DrawingEngine::IsParallelAccessLocked() const
{
	return fGraphicsCard->IsParallelAccessLocked();
}
#endif


void
DrawingEngine::UnlockParallelAccess()
{
	fGraphicsCard->UnlockParallelAccess();
}


bool
DrawingEngine::LockExclusiveAccess()
{
	return fGraphicsCard->LockExclusiveAccess();
}


bool
DrawingEngine::IsExclusiveAccessLocked() const
{
	return fGraphicsCard->IsExclusiveAccessLocked();
}


void
DrawingEngine::UnlockExclusiveAccess()
{
	fGraphicsCard->UnlockExclusiveAccess();
}


// #pragma mark -


void
DrawingEngine::FrameBufferChanged()
{
	if (!fGraphicsCard) {
		fPainter->DetachFromBuffer();
		return;
	}

	// NOTE: locking is probably bogus, since we are called
	// in the thread that changed the frame buffer...
	if (LockExclusiveAccess()) {
		fPainter->SetDeviceScale(fGraphicsCard->RenderScaleFactor());
		fPainter->AttachToBuffer(fGraphicsCard->DrawingBuffer());
		UnlockExclusiveAccess();
	}
}


void
DrawingEngine::SetHWInterface(HWInterface* interface)
{
	if (fGraphicsCard == interface)
		return;

	if (fGraphicsCard)
		fGraphicsCard->RemoveListener(this);

	fGraphicsCard = interface;

	if (fGraphicsCard)
		fGraphicsCard->AddListener(this);

	FrameBufferChanged();
}


void
DrawingEngine::SetCopyToFrontEnabled(bool enable)
{
	fCopyToFront = enable;
}


void
DrawingEngine::CopyToFront(/*const*/ BRegion& region)
{
	if (fPainter->DeviceScale() != 1) {
		BRegion scaled;
		_ScaleRegion(region, scaled);
		fGraphicsCard->InvalidateRegion(scaled);
		return;
	}
	fGraphicsCard->InvalidateRegion(region);
}


float
DrawingEngine::RenderScale() const
{
	return fPainter->DeviceScale();
}


/*!	A logical region in buffer pixels, with the Painter's rounding.
*/
void
DrawingEngine::_ScaleRegion(const BRegion& region, BRegion& scaled) const
{
	float scale = fPainter->DeviceScale();
	scaled.MakeEmpty();
	int32 count = region.CountRects();
	for (int32 i = 0; i < count; i++) {
		clipping_rect r = region.RectAtInt(i);
		r.left = (int32)floorf(r.left * scale);
		r.top = (int32)floorf(r.top * scale);
		r.right = (int32)floorf((r.right + 1) * scale) - 1;
		r.bottom = (int32)floorf((r.bottom + 1) * scale) - 1;
		scaled.Include(r);
	}
}


// #pragma mark -


//! the DrawingEngine needs to be locked!
void
DrawingEngine::ConstrainClippingRegion(const BRegion* region)
{
	ASSERT_PARALLEL_LOCKED();

	fPainter->ConstrainClipping(region);
}


void
DrawingEngine::SetDrawState(const DrawState* state, int32 xOffset,
	int32 yOffset)
{
	fPainter->SetDrawState(state, xOffset, yOffset);
}


void
DrawingEngine::SetHighColor(const rgb_color& color)
{
	fPainter->SetHighColor(color);
}


void
DrawingEngine::SetLowColor(const rgb_color& color)
{
	fPainter->SetLowColor(color);
}


void
DrawingEngine::SetPenSize(float size)
{
	fPainter->SetPenSize(size);
}


void
DrawingEngine::SetStrokeMode(cap_mode lineCap, join_mode joinMode,
	float miterLimit)
{
	fPainter->SetStrokeMode(lineCap, joinMode, miterLimit);
}


void
DrawingEngine::SetFillRule(int32 fillRule)
{
	fPainter->SetFillRule(fillRule);
}


void
DrawingEngine::SetBlendingMode(source_alpha srcAlpha, alpha_function alphaFunc)
{
	fPainter->SetBlendingMode(srcAlpha, alphaFunc);
}


void
DrawingEngine::SetPattern(const struct pattern& pattern)
{
	fPainter->SetPattern(pattern);
}


void
DrawingEngine::SetDrawingMode(drawing_mode mode)
{
	fPainter->SetDrawingMode(mode);
}


void
DrawingEngine::SetDrawingMode(drawing_mode mode, drawing_mode& oldMode)
{
	oldMode = fPainter->DrawingMode();
	fPainter->SetDrawingMode(mode);
}


void
DrawingEngine::SetFont(const ServerFont& font)
{
	fPainter->SetFont(font);
}


void
DrawingEngine::SetFont(const DrawState* state)
{
	fPainter->SetFont(state);
}


void
DrawingEngine::SetTransform(const BAffineTransform& transform, int32 xOffset,
	int32 yOffset)
{
	fPainter->SetTransform(transform, xOffset, yOffset);
}


// #pragma mark -


// CopyRegion() does a topological sort of the rects in the
// region. The algorithm was suggested by Ingo Weinhold.
// It compares each rect with each rect and builds a tree
// of successors so we know the order in which they can be copied.
// For example, let's suppose these rects are in a BRegion:
//                        ************
//                        *    B     *
//                        ************
//      *************
//      *           *
//      *     A     ****************
//      *           **             *
//      **************             *
//                   *     C       *
//                   *             *
//                   *             *
//                   ***************
// When copying stuff from LEFT TO RIGHT, TOP TO BOTTOM, the
// result of the sort will be C, A, B. For this direction, we search
// for the rects that have no neighbors to their right and to their
// bottom, These can be copied without drawing into the area of
// rects yet to be copied. If you move from RIGHT TO LEFT, BOTTOM TO TOP,
// you go look for the ones that have no neighbors to their top and left.
//
// Here I draw some rays to illustrate LEFT TO RIGHT, TOP TO BOTTOM:
//                        ************
//                        *    B     *
//                        ************
//      *************
//      *           *
//      *     A     ****************-----------------
//      *           **             *
//      **************             *
//                   *     C       *
//                   *             *
//                   *             *
//                   ***************
//                   |
//                   |
//                   |
//                   |
// There are no rects in the area defined by the rays to the right
// and bottom of rect C, so that's the one we want to copy first
// (for positive x and y offsets).
// Since A is to the left of C and B is to the top of C, The "node"
// for C will point to the nodes of A and B as its "successors". Therefore,
// A and B will have an "indegree" of 1 for C pointing to them. C will
// have an "indegree" of 0, because there was no rect to which C
// was to the left or top of. When comparing A and B, neither is left
// or top from the other and in the sense that the algorithm cares about.

// NOTE: comparison of coordinates assumes that rects don't overlap
// and don't share the actual edge either (as is the case in BRegions).

struct node {
	node()
	{
		pointers = NULL;
	}

	node(const BRect& r, int32 maxPointers)
	{
		init(r, maxPointers);
	}

	~node()
	{
		delete [] pointers;
	}

	void init(const BRect& r, int32 maxPointers)
	{
		rect = r;
		pointers = new(std::nothrow) node*[maxPointers];
		in_degree = 0;
		next_pointer = 0;
	}

	void push(node* node)
	{
		pointers[next_pointer] = node;
		next_pointer++;
	}

	node* top()
	{
		return pointers[next_pointer];
	}

	node* pop()
	{
		node* ret = top();
		next_pointer--;
		return ret;
	}

	BRect	rect;
	int32	in_degree;
	node**	pointers;
	int32	next_pointer;
};


static bool
is_left_of(const BRect& a, const BRect& b)
{
	return (a.right < b.left);
}


static bool
is_above(const BRect& a, const BRect& b)
{
	return (a.bottom < b.top);
}


bool
DrawingEngine::CopyRegion(/*const*/ BRegion* region, int32 xOffset,
	int32 yOffset)
{
	// NOTE: region is already clipped
	ASSERT_PARALLEL_LOCKED();

	if (fPainter->DeviceScale() != 1) {
		// A copy is exact only when the translation is an integer number of
		// device pixels. Rounding each movement accumulates error and changes
		// the phase of borders relative to clipping and subsequent drawing.
		float deviceX = xOffset * fPainter->DeviceScale();
		float deviceY = yOffset * fPainter->DeviceScale();
		if (deviceX != floorf(deviceX) || deviceY != floorf(deviceY))
			return false;
		return CopyRegionByDeviceOffset(region, (int32)deviceX,
			(int32)deviceY);
	}
	return _CopyRegion(region, xOffset, yOffset);
}


/*!	Moves the pixels of a logical region by a number of buffer pixels, which
	is what the caller decided the move is worth at the drawing density.
	Nothing outside the region scaled to buffer pixels and moved is touched.
*/
bool
DrawingEngine::CopyRegionByDeviceOffset(/*const*/ BRegion* region,
	int32 deviceX, int32 deviceY)
{
	ASSERT_PARALLEL_LOCKED();

	if (fPainter->DeviceScale() == 1)
		return _CopyRegion(region, deviceX, deviceY);

	// pixels move in the buffer, which is finer than the region
	BRegion scaledRegion;
	_ScaleRegion(*region, scaledRegion);
	return _CopyRegion(&scaledRegion, deviceX, deviceY);
}


bool
DrawingEngine::_CopyRegion(/*const*/ BRegion* region, int32 xOffset,
	int32 yOffset)
{
	BRect frame = region->Frame();
	frame = frame | frame.OffsetByCopy(xOffset, yOffset);
	if (!fGraphicsCard->DrawingBuffer()->Bounds().Contains(frame))
		return false;

	AutoFloatingOverlaysHider _(fGraphicsCard, frame);

	// what moves is the screen's contents, also where a direct window left
	// the drawing buffer out of date
	fGraphicsCard->RefreshStale(*region);

	int32 count = region->CountRects();

	// TODO: make this step unnecessary
	// (by using different stack impl inside node)
	BStackOrHeapArray<node, 64> nodes(count);
	for (int32 i= 0; i < count; i++) {
		nodes[i].init(region->RectAt(i), count);
		if (nodes[i].pointers == NULL)
			return false;
	}

	for (int32 i = 0; i < count; i++) {
		BRect a = region->RectAt(i);
		for (int32 k = i + 1; k < count; k++) {
			BRect b = region->RectAt(k);
			int cmp = 0;
			// compare horizontally
			if (xOffset > 0) {
				if (is_left_of(a, b)) {
					cmp -= 1;
				} else if (is_left_of(b, a)) {
					cmp += 1;
				}
			} else if (xOffset < 0) {
				if (is_left_of(a, b)) {
					cmp += 1;
				} else if (is_left_of(b, a)) {
					cmp -= 1;
				}
			}
			// compare vertically
			if (yOffset > 0) {
				if (is_above(a, b)) {
					cmp -= 1;
				} else if (is_above(b, a)) {
					cmp += 1;
				}
			} else if (yOffset < 0) {
				if (is_above(a, b)) {
					cmp += 1;
				} else if (is_above(b, a)) {
					cmp -= 1;
				}
			}
			// add appropriate node as successor
			if (cmp > 0) {
				nodes[i].push(&nodes[k]);
				nodes[k].in_degree++;
			} else if (cmp < 0) {
				nodes[k].push(&nodes[i]);
				nodes[i].in_degree++;
			}
		}
	}
	// put all nodes onto a stack that have an "indegree" count of zero
	std::stack<node*> inDegreeZeroNodes;
	for (int32 i = 0; i < count; i++) {
		if (nodes[i].in_degree == 0) {
			inDegreeZeroNodes.push(&nodes[i]);
		}
	}
	// pop the rects from the stack, do the actual copy operation
	// and decrease the "indegree" count of the other rects not
	// currently on the stack and to which the current rect pointed
	// to. If their "indegree" count reaches zero, put them onto the
	// stack as well.

	while (!inDegreeZeroNodes.empty()) {
		node* n = inDegreeZeroNodes.top();
		inDegreeZeroNodes.pop();

		BRect touched = CopyRect(n->rect, xOffset, yOffset);
		fGraphicsCard->Invalidate(touched);

		for (int32 k = 0; k < n->next_pointer; k++) {
			n->pointers[k]->in_degree--;
			if (n->pointers[k]->in_degree == 0)
				inDegreeZeroNodes.push(n->pointers[k]);
		}
	}
	return true;
}


void
DrawingEngine::InvertRect(BRect r)
{
	ASSERT_PARALLEL_LOCKED();

	make_rect_valid(r);
	// NOTE: Currently ignores view transformation, so no TransformAndClipRect()
	DrawTransaction transaction(this, fPainter->ClipLogicalRect(r));
	if (!transaction.IsDirty())
		return;

	fPainter->InvertRect(r);
}


void
DrawingEngine::DrawBitmap(ServerBitmap* bitmap, const BRect& bitmapRect,
	const BRect& viewRect, uint32 options)
{
	ASSERT_PARALLEL_LOCKED();

	BRect clipped = (options & B_DEVICE_PIXEL_BITMAP) != 0
			&& fPainter->HasIdentityViewTransform()
		? fPainter->ClipRect(fPainter->DeviceBitmapRect(bitmapRect, viewRect))
		: fPainter->TransformAndClipRect(viewRect);
	DrawTransaction transaction(this, clipped);
	if (transaction.IsDirty())
		fPainter->DrawBitmap(bitmap, bitmapRect, viewRect, options);
}


void
DrawingEngine::DrawArc(BRect r, const float& angle, const float& span,
	bool filled)
{
	ASSERT_PARALLEL_LOCKED();

	make_rect_valid(r);
	fPainter->AlignEllipseRect(&r, filled);

	BRect clipped(r);
	if (!filled)
		extend_by_stroke_width(clipped, fPainter->PenSize());
	DrawTransaction transaction(this, fPainter->TransformAndClipRect(clipped));
	if (!transaction.IsDirty())
		return;

	float xRadius = r.Width() / 2.0;
	float yRadius = r.Height() / 2.0;
	BPoint center(r.left + xRadius,
				  r.top + yRadius);

	if (filled)
		fPainter->FillArc(center, xRadius, yRadius, angle, span);
	else
		fPainter->StrokeArc(center, xRadius, yRadius, angle, span);
}


void
DrawingEngine::DrawArc(BRect r, const float& angle, const float& span,
	bool filled, const BGradient& gradient)
{
	ASSERT_PARALLEL_LOCKED();

	make_rect_valid(r);
	fPainter->AlignEllipseRect(&r, true);
	DrawTransaction transaction(this, fPainter->TransformAndClipRect(r));
	if (!transaction.IsDirty())
		return;

	float xRadius = r.Width() / 2.0;
	float yRadius = r.Height() / 2.0;
	BPoint center(r.left + xRadius,
				  r.top + yRadius);

	if (filled)
		fPainter->FillArc(center, xRadius, yRadius, angle, span, gradient);
	else
		fPainter->StrokeArc(center, xRadius, yRadius, angle, span, gradient);
}


void
DrawingEngine::DrawBezier(BPoint* pts, bool filled)
{
	ASSERT_PARALLEL_LOCKED();

	// TODO: figure out bounds and hide cursor depending on that
	DrawTransaction transaction(this);

	transaction.SetDirty(fPainter->DrawBezier(pts, filled));
}


void
DrawingEngine::DrawBezier(BPoint* pts, bool filled, const BGradient& gradient)
{
	ASSERT_PARALLEL_LOCKED();

	// TODO: figure out bounds and hide cursor depending on that
	DrawTransaction transaction(this);

	transaction.SetDirty(fPainter->DrawBezier(pts, filled, gradient));
}


void
DrawingEngine::DrawEllipse(BRect r, bool filled)
{
	ASSERT_PARALLEL_LOCKED();

	make_rect_valid(r);
	BRect clipped = r;
	fPainter->AlignEllipseRect(&clipped, filled);

	if (!filled)
		extend_by_stroke_width(clipped, fPainter->PenSize());

	clipped.left = floorf(clipped.left);
	clipped.top = floorf(clipped.top);
	clipped.right = ceilf(clipped.right);
	clipped.bottom = ceilf(clipped.bottom);

	DrawTransaction transaction(this, fPainter->TransformAndClipRect(clipped));
	if (!transaction.IsDirty())
		return;

	fPainter->DrawEllipse(r, filled);
}


void
DrawingEngine::DrawEllipse(BRect r, bool filled, const BGradient& gradient)
{
	ASSERT_PARALLEL_LOCKED();

	make_rect_valid(r);
	BRect clipped = r;
	fPainter->AlignEllipseRect(&clipped, filled);

	if (!filled)
		extend_by_stroke_width(clipped, fPainter->PenSize());

	clipped.left = floorf(clipped.left);
	clipped.top = floorf(clipped.top);
	clipped.right = ceilf(clipped.right);
	clipped.bottom = ceilf(clipped.bottom);

	DrawTransaction transaction(this, fPainter->TransformAndClipRect(clipped));
	if (!transaction.IsDirty())
		return;

	fPainter->DrawEllipse(r, filled, gradient);
}


void
DrawingEngine::DrawPolygon(BPoint* ptlist, int32 numpts, BRect bounds,
	bool filled, bool closed)
{
	ASSERT_PARALLEL_LOCKED();

	make_rect_valid(bounds);
	if (!filled)
		extend_by_stroke_width(bounds, fPainter->PenSize());
	DrawTransaction transaction(this, fPainter->TransformAndClipRect(bounds));
	if (!transaction.IsDirty())
		return;

	fPainter->DrawPolygon(ptlist, numpts, filled, closed);
}


void
DrawingEngine::DrawPolygon(BPoint* ptlist, int32 numpts, BRect bounds,
	bool filled, bool closed, const BGradient& gradient)
{
	ASSERT_PARALLEL_LOCKED();

	make_rect_valid(bounds);
	if (!filled)
		extend_by_stroke_width(bounds, fPainter->PenSize());
	DrawTransaction transaction(this, fPainter->TransformAndClipRect(bounds));
	if (!transaction.IsDirty())
		return;

	fPainter->DrawPolygon(ptlist, numpts, filled, closed, gradient);
}


// #pragma mark - rgb_color


void
DrawingEngine::StrokePoint(const BPoint& pt, const rgb_color& color)
{
	StrokeLine(pt, pt, color);
}


/*!	This function is only used by Decorators,
	it assumes a one pixel wide line
*/
void
DrawingEngine::StrokeLine(const BPoint& start, const BPoint& end,
	const rgb_color& color)
{
	ASSERT_PARALLEL_LOCKED();

	BRect touched(start, end);
	make_rect_valid(touched);
	touched = fPainter->ClipLogicalRect(touched);
	DrawTransaction transaction(this, touched);

	if (!fPainter->StraightLine(start, end, color)) {
		rgb_color previousColor = fPainter->HighColor();
		drawing_mode previousMode = fPainter->DrawingMode();

		fPainter->SetHighColor(color);
		fPainter->SetDrawingMode(B_OP_OVER);
		fPainter->StrokeLine(start, end);

		fPainter->SetDrawingMode(previousMode);
		fPainter->SetHighColor(previousColor);
	}
}


//!	This function is used to draw a one pixel wide rect
void
DrawingEngine::StrokeRect(BRect r, const rgb_color& color)
{
	ASSERT_PARALLEL_LOCKED();

	make_rect_valid(r);
	DrawTransaction transaction(this, fPainter->ClipLogicalRect(r));
	if (!transaction.IsDirty())
		return;

	fPainter->StrokeRect(r, color);
}


void
DrawingEngine::FillRect(BRect r, const rgb_color& color)
{
	ASSERT_PARALLEL_LOCKED();

	make_rect_valid(r);
	DrawTransaction transaction(this, fPainter->ClipLogicalRect(r));
	if (!transaction.IsDirty())
		return;

	fPainter->FillRect(r, color);
}


void
DrawingEngine::FillRegion(BRegion& r, const rgb_color& color)
{
	ASSERT_PARALLEL_LOCKED();

	// NOTE: region expected to be already clipped correctly!!
	BRect frame = r.Frame();
	if (!fPainter->Bounds().Contains(frame)) {
		// NOTE: I am not quite sure yet how this can happen, but apparently it
		// can (see bug #634).
		// This function is used for internal app_server painting, in the case of
		// bug #634, the background of views is painted. But the view region
		// should never be outside the frame buffer bounds.
//		char message[1024];
//		BRect bounds = fPainter->Bounds();
//		sprintf(message, "FillRegion() - painter: (%d, %d)->(%d, %d), region: (%d, %d)->(%d, %d)",
//			(int)bounds.left, (int)bounds.top, (int)bounds.right, (int)bounds.bottom,
//			(int)frame.left, (int)frame.top, (int)frame.right, (int)frame.bottom);
//		debugger(message);
		return;
	}

	BRegion scaled;
	if (fPainter->DeviceScale() != 1)
		_ScaleRegion(r, scaled);
	DrawTransaction transaction(this,
		fPainter->DeviceScale() != 1 ? scaled : r);

	int32 count = r.CountRects();
	for (int32 i = 0; i < count; i++)
		fPainter->FillRectNoClipping(r.RectAtInt(i), color);
}


// #pragma mark - DrawState


void
DrawingEngine::StrokeRect(BRect r)
{
	ASSERT_PARALLEL_LOCKED();

	// support invalid rects
	make_rect_valid(r);
	BRect clipped(r);
	extend_by_stroke_width(clipped, fPainter->PenSize());
	DrawTransaction transaction(this, fPainter->TransformAndClipRect(clipped));
	if (!transaction.IsDirty())
		return;

	fPainter->StrokeRect(r);
}


void
DrawingEngine::FillRect(BRect r)
{
	ASSERT_PARALLEL_LOCKED();

	make_rect_valid(r);

	r = fPainter->AlignRect(r);

	DrawTransaction transaction(this, fPainter->TransformAndClipRect(r));
	if (!transaction.IsDirty())
		return;

	fPainter->FillRect(r);
}


void
DrawingEngine::StrokeRect(BRect r, const BGradient& gradient)
{
	ASSERT_PARALLEL_LOCKED();

	// support invalid rects
	make_rect_valid(r);
	BRect clipped(r);
	extend_by_stroke_width(clipped, fPainter->PenSize());
	DrawTransaction transaction(this, fPainter->TransformAndClipRect(clipped));
	if (!transaction.IsDirty())
		return;

	fPainter->StrokeRect(r, gradient);
}


void
DrawingEngine::FillRect(BRect r, const BGradient& gradient)
{
	ASSERT_PARALLEL_LOCKED();

	make_rect_valid(r);
	r = fPainter->AlignRect(r);

	DrawTransaction transaction(this, fPainter->TransformAndClipRect(r));
	if (!transaction.IsDirty())
		return;

	fPainter->FillRect(r, gradient);
}


void
DrawingEngine::FillRegion(BRegion& r)
{
	ASSERT_PARALLEL_LOCKED();

	BRect clipped = fPainter->TransformAndClipRect(r.Frame());
	DrawTransaction transaction(this, clipped);
	if (!transaction.IsDirty())
		return;

	int32 count = r.CountRects();
	for (int32 i = 0; i < count; i++)
		fPainter->FillRect(r.RectAt(i));
}


void
DrawingEngine::FillRegion(BRegion& r, const BGradient& gradient)
{
	ASSERT_PARALLEL_LOCKED();

	BRect clipped = fPainter->TransformAndClipRect(r.Frame());
	DrawTransaction transaction(this, clipped);
	if (!transaction.IsDirty())
		return;

	int32 count = r.CountRects();
	for (int32 i = 0; i < count; i++)
		fPainter->FillRect(r.RectAt(i), gradient);
}


void
DrawingEngine::DrawRoundRect(BRect r, float xrad, float yrad, bool filled)
{
	ASSERT_PARALLEL_LOCKED();

	make_rect_valid(r);
	if (!filled)
		extend_by_stroke_width(r, fPainter->PenSize());
	BRect clipped = fPainter->TransformAndClipRect(r);

	clipped.left = floorf(clipped.left);
	clipped.top = floorf(clipped.top);
	clipped.right = ceilf(clipped.right);
	clipped.bottom = ceilf(clipped.bottom);

	DrawTransaction transaction(this, clipped);
	if (!transaction.IsDirty())
		return;

	if (filled)
		fPainter->FillRoundRect(r, xrad, yrad);
	else
		fPainter->StrokeRoundRect(r, xrad, yrad);
}


void
DrawingEngine::DrawRoundRect(BRect r, float xrad, float yrad,
	bool filled, const BGradient& gradient)
{
	ASSERT_PARALLEL_LOCKED();

	make_rect_valid(r);
	if (!filled)
		extend_by_stroke_width(r, fPainter->PenSize());
	BRect clipped = fPainter->TransformAndClipRect(r);

	clipped.left = floorf(clipped.left);
	clipped.top = floorf(clipped.top);
	clipped.right = ceilf(clipped.right);
	clipped.bottom = ceilf(clipped.bottom);

	DrawTransaction transaction(this, clipped);
	if (!transaction.IsDirty())
		return;

	if (filled)
		fPainter->FillRoundRect(r, xrad, yrad, gradient);
	else
		fPainter->StrokeRoundRect(r, xrad, yrad, gradient);
}


void
DrawingEngine::DrawShape(const BRect& bounds, int32 opCount,
	const uint32* opList, int32 ptCount, const BPoint* ptList, bool filled,
	const BPoint& viewToScreenOffset, float viewScale)
{
	ASSERT_PARALLEL_LOCKED();

// TODO: bounds probably does not take curves and arcs into account...
//	BRect clipped(bounds);
//	if (!filled)
//		extend_by_stroke_width(clipped, fPainter->PenSize());
//	clipped = fPainter->TransformAndClipRect(bounds);
//
//	clipped.left = floorf(clipped.left);
//	clipped.top = floorf(clipped.top);
//	clipped.right = ceilf(clipped.right);
//	clipped.bottom = ceilf(clipped.bottom);
//
//	DrawTransaction transaction(this, clipped);
//	if (!transaction.IsDirty())
//		return;
	DrawTransaction transaction(this);

	transaction.SetDirty(fPainter->DrawShape(opCount, opList, ptCount, ptList,
		filled, viewToScreenOffset, viewScale));
}


void
DrawingEngine::DrawShape(const BRect& bounds, int32 opCount,
	const uint32* opList, int32 ptCount, const BPoint* ptList,
	bool filled, const BGradient& gradient, const BPoint& viewToScreenOffset,
	float viewScale)
{
	ASSERT_PARALLEL_LOCKED();

// TODO: bounds probably does not take curves and arcs into account...
//	BRect clipped = fPainter->TransformAndClipRect(bounds);
//
//	clipped.left = floorf(clipped.left);
//	clipped.top = floorf(clipped.top);
//	clipped.right = ceilf(clipped.right);
//	clipped.bottom = ceilf(clipped.bottom);
//
//	DrawTransaction transaction(this, clipped);
//	if (!transaction.IsDirty())
//		return;
	DrawTransaction transaction(this);

	transaction.SetDirty(fPainter->DrawShape(opCount, opList, ptCount, ptList,
		filled, gradient, viewToScreenOffset, viewScale));
}


void
DrawingEngine::DrawTriangle(BPoint* pts, const BRect& bounds, bool filled)
{
	ASSERT_PARALLEL_LOCKED();

	BRect clipped(bounds);
	if (!filled)
		extend_by_stroke_width(clipped, fPainter->PenSize());
	DrawTransaction transaction(this, fPainter->TransformAndClipRect(clipped));
	if (!transaction.IsDirty())
		return;

	if (filled)
		fPainter->FillTriangle(pts[0], pts[1], pts[2]);
	else
		fPainter->StrokeTriangle(pts[0], pts[1], pts[2]);
}


void
DrawingEngine::DrawTriangle(BPoint* pts, const BRect& bounds,
	bool filled, const BGradient& gradient)
{
	ASSERT_PARALLEL_LOCKED();

	BRect clipped(bounds);
	if (!filled)
		extend_by_stroke_width(clipped, fPainter->PenSize());
	DrawTransaction transaction(this, fPainter->TransformAndClipRect(clipped));
	if (!transaction.IsDirty())
		return;

	if (filled)
		fPainter->FillTriangle(pts[0], pts[1], pts[2], gradient);
	else
		fPainter->StrokeTriangle(pts[0], pts[1], pts[2], gradient);
}


void
DrawingEngine::StrokeLine(const BPoint& start, const BPoint& end)
{
	ASSERT_PARALLEL_LOCKED();

	BRect touched(start, end);
	make_rect_valid(touched);
	extend_by_stroke_width(touched, fPainter->PenSize());
	DrawTransaction transaction(this, fPainter->TransformAndClipRect(touched));
	if (!transaction.IsDirty())
		return;

	fPainter->StrokeLine(start, end);
}


void
DrawingEngine::StrokeLine(const BPoint& start, const BPoint& end, const BGradient& gradient)
{
	ASSERT_PARALLEL_LOCKED();

	BRect touched(start, end);
	make_rect_valid(touched);
	extend_by_stroke_width(touched, fPainter->PenSize());
	DrawTransaction transaction(this, fPainter->TransformAndClipRect(touched));
	if (!transaction.IsDirty())
		return;

	fPainter->StrokeLine(start, end, gradient);
}


void
DrawingEngine::StrokeLineArray(int32 numLines,
	const ViewLineArrayInfo *lineData)
{
	ASSERT_PARALLEL_LOCKED();

	if (!lineData || numLines <= 0)
		return;

	// figure out bounding box for line array
	const ViewLineArrayInfo* data = (const ViewLineArrayInfo*)&lineData[0];
	BRect touched(min_c(data->startPoint.x, data->endPoint.x),
		min_c(data->startPoint.y, data->endPoint.y),
		max_c(data->startPoint.x, data->endPoint.x),
		max_c(data->startPoint.y, data->endPoint.y));

	for (int32 i = 1; i < numLines; i++) {
		data = (const ViewLineArrayInfo*)&lineData[i];
		BRect box(min_c(data->startPoint.x, data->endPoint.x),
			min_c(data->startPoint.y, data->endPoint.y),
			max_c(data->startPoint.x, data->endPoint.x),
			max_c(data->startPoint.y, data->endPoint.y));
		touched = touched | box;
	}
	extend_by_stroke_width(touched, fPainter->PenSize());
	DrawTransaction transaction(this, fPainter->TransformAndClipRect(touched));
	if (!transaction.IsDirty())
		return;

	data = (const ViewLineArrayInfo*)&(lineData[0]);

	// store current graphics state, we mess with the
	// high color and pattern...
	rgb_color oldColor = fPainter->HighColor();
	struct pattern pattern = fPainter->Pattern();

	fPainter->SetHighColor(data->color);
	fPainter->SetPattern(B_SOLID_HIGH);
	fPainter->StrokeLine(data->startPoint, data->endPoint);

	for (int32 i = 1; i < numLines; i++) {
		data = (const ViewLineArrayInfo*)&(lineData[i]);
		fPainter->SetHighColor(data->color);
		fPainter->StrokeLine(data->startPoint, data->endPoint);
	}

	// restore correct drawing state highcolor and pattern
	fPainter->SetHighColor(oldColor);
	fPainter->SetPattern(pattern);
}


namespace {

/*!	The buffer pixels of the logical pixels along one axis of a frame whose
	rings are drawn with StrokeFrameLineArray(): the rings at either end get
	the pixels of the band they cover, shared out by
	distribute_ring_widths(), and every other pixel keeps its usual ones.
*/
class FrameAxis {
public:
	FrameAxis(int32 low, int32 high, int32 rings, bool hasLow, bool hasHigh,
		uint32 uniformRings, bool overFill, bool coverPixels, bool hugInside,
		bool outerSlack, float scale)
		:
		fLow(low),
		fHigh(high),
		fRings(rings),
		fHasLow(hasLow),
		fHasHigh(hasHigh),
		fScale(scale)
	{
		int32 widths[kMaxRings];
		if (!overFill && hugInside) {
			int32 base = max_c(1, (int32)floorf(fScale));
			fLowEdges[rings] = _Edge(low + rings);
			fHighEdges[rings] = _Edge(high + 1 - rings);
			for (int32 i = rings - 1; i >= 0; i--) {
				fLowEdges[i] = fLowEdges[i + 1] - base;
				fHighEdges[i] = fHighEdges[i + 1] + base;
			}
			return;
		}

		fLowEdges[0] = _Edge(low);
		if (overFill)
			_Fixed(uniformRings, widths);
		else if (outerSlack)
			_Slack(_Edge(low + rings) - fLowEdges[0], uniformRings, widths);
		else {
			_Distribute(_Edge(low + rings) - fLowEdges[0], uniformRings,
				widths);
		}
		for (int32 i = 0; i < rings; i++) {
			fLowEdges[i + 1] = fLowEdges[i] + widths[i];
			if (overFill && coverPixels)
				fLowEdges[i + 1] = max_c(fLowEdges[i + 1], _Edge(low + i + 1));
		}
		fHighEdges[0] = _Edge(high + 1);
		if (!overFill && outerSlack) {
			_Slack(fHighEdges[0] - _Edge(high + 1 - rings), uniformRings,
				widths);
		} else if (!overFill) {
			_Distribute(fHighEdges[0] - _Edge(high + 1 - rings), uniformRings,
				widths);
		}
		for (int32 i = 0; i < rings; i++) {
			fHighEdges[i + 1] = fHighEdges[i] - widths[i];
			if (overFill && coverPixels)
				fHighEdges[i + 1] = min_c(fHighEdges[i + 1], _Edge(high - i));
		}
	}

	static const int32 kMaxRings = 8;

	//! The buffer pixels [start, end) of logical pixel \a pixel.
	void Span(int32 pixel, int32& start, int32& end) const
	{
		if (fHasLow && pixel >= fLow && pixel < fLow + fRings) {
			start = fLowEdges[pixel - fLow];
			end = fLowEdges[pixel - fLow + 1];
		} else if (fHasHigh && pixel <= fHigh && pixel > fHigh - fRings) {
			start = fHighEdges[fHigh - pixel + 1];
			end = fHighEdges[fHigh - pixel];
		} else {
			start = _Edge(pixel);
			end = _Edge(pixel + 1);
		}
	}

private:
	int32 _Edge(int32 pixel) const
	{
		return (int32)floorf(pixel * fScale);
	}

	/*!	Over a fill nothing needs to meet the inner edge: the rings that keep
		their width get the scale rounded, the others its whole part.
	*/
	void _Fixed(uint32 uniformRings, int32* widths) const
	{
		int32 base = max_c(1, (int32)floorf(fScale));
		int32 rounded = max_c(1, (int32)floorf(fScale + 0.5f));
		for (int32 i = 0; i < fRings; i++)
			widths[i] = (uniformRings & (1 << i)) != 0 ? rounded : base;
	}

	/*!	The rings get fixed widths (see _Fixed()) and the outermost one what
		they leave of \a total; when they do not fit, as _Distribute().
	*/
	void _Slack(int32 total, uint32 uniformRings, int32* widths) const
	{
		_Fixed(uniformRings, widths);
		int32 fixed = 0;
		for (int32 i = 1; i < fRings; i++)
			fixed += widths[i];
		if (fRings < 2 || total - fixed < 1) {
			_Distribute(total, uniformRings, widths);
			return;
		}
		widths[0] = total - fixed;
	}

	/*!	Every ring gets the whole part of the scale; of the pixels left over,
		the rings that keep their width get one each first when the scale
		rounds up, then the others, outermost first.
	*/
	void _Distribute(int32 total, uint32 uniformRings, int32* widths) const
	{
		int32 base = max_c(1, (int32)floorf(fScale));
		int32 extra = total - fRings * base;
		for (int32 i = 0; i < fRings; i++)
			widths[i] = base;
		if (floorf(fScale + 0.5f) > base) {
			for (int32 i = 0; i < fRings && extra > 0; i++) {
				if ((uniformRings & (1 << i)) != 0) {
					widths[i]++;
					extra--;
				}
			}
		}
		for (int32 i = 0; i < fRings && extra > 0; i++) {
			if ((uniformRings & (1 << i)) == 0) {
				widths[i]++;
				extra--;
			}
		}
		for (int32 i = 0; i < fRings && extra > 0; i++) {
			widths[i]++;
			extra--;
		}
		for (int32 i = fRings - 1; i >= 0 && extra < 0; i--) {
			// cannot happen with a scale above 1, but never go negative
			int32 take = min_c(widths[i] - 1, -extra);
			widths[i] -= take;
			extra += take;
		}
	}

	int32	fLow;
	int32	fHigh;
	int32	fRings;
	bool	fHasLow;
	bool	fHasHigh;
	float	fScale;
	int32	fLowEdges[kMaxRings + 1];
	int32	fHighEdges[kMaxRings + 1];
};

}	// namespace


/*!	Draws the lines of a frame's rings (see ViewFrameLineArrayInfo). At a
	fractional scale one logical pixel is one buffer pixel in some places and
	two in others, so the same ring would be as thick as one on one side of
	a control and as two on the other. Here the band the rings cover keeps
	its edges, so that it still meets what is drawn around and inside it,
	and its pixels are shared out the same way on every side.
*/
void
DrawingEngine::StrokeFrameLineArray(const ViewFrameLineArrayInfo& info,
	const ViewLineArrayInfo* lines)
{
	ASSERT_PARALLEL_LOCKED();

	float scale = fPainter->DeviceScale();
	BRect frame = info.frame;
	int32 rings = info.rings;
	// the bands at opposite sides must not overlap
	int32 horizontalBands
		= ((info.borders & BControlLook::B_LEFT_BORDER) != 0 ? 1 : 0)
		+ ((info.borders & BControlLook::B_RIGHT_BORDER) != 0 ? 1 : 0);
	int32 verticalBands
		= ((info.borders & BControlLook::B_TOP_BORDER) != 0 ? 1 : 0)
		+ ((info.borders & BControlLook::B_BOTTOM_BORDER) != 0 ? 1 : 0);
	if (scale == floorf(scale) || !fPainter->HasIdentityViewTransform()
		|| rings < 1 || rings > FrameAxis::kMaxRings
		|| frame.Width() + 1 < horizontalBands * rings
		|| frame.Height() + 1 < verticalBands * rings) {
		StrokeLineArray(info.lineCount, lines);
		return;
	}

	bool overFill = (info.flags & kFrameLinesOverFill) != 0;
	bool coverPixels = (info.flags & kFrameLinesCoverPixels) != 0;
	bool hugInside = (info.flags & kFrameLinesHugInside) != 0;
	bool outerSlack = (info.flags & kFrameLinesOuterSlack) != 0;
	FrameAxis horizontal((int32)frame.left, (int32)frame.right, rings,
		(info.borders & BControlLook::B_LEFT_BORDER) != 0,
		(info.borders & BControlLook::B_RIGHT_BORDER) != 0,
		info.uniformRings, overFill, coverPixels, hugInside, outerSlack, scale);
	FrameAxis vertical((int32)frame.top, (int32)frame.bottom, rings,
		(info.borders & BControlLook::B_TOP_BORDER) != 0,
		(info.borders & BControlLook::B_BOTTOM_BORDER) != 0,
		info.uniformRings, overFill, coverPixels, hugInside, outerSlack, scale);

	BStackOrHeapArray<BRect, 64> deviceRects(info.lineCount);
	if (!deviceRects.IsValid())
		return;

	BRect touched;
	for (int32 i = 0; i < info.lineCount; i++) {
		BPoint a = lines[i].startPoint;
		BPoint b = lines[i].endPoint;
		if (a.x != b.x && a.y != b.y) {
			deviceRects[i] = BRect();
			continue;
		}
		int32 left, right, top, bottom, ignore;
		horizontal.Span((int32)floorf(min_c(a.x, b.x)), left, ignore);
		horizontal.Span((int32)floorf(max_c(a.x, b.x)), ignore, right);
		vertical.Span((int32)floorf(min_c(a.y, b.y)), top, ignore);
		vertical.Span((int32)floorf(max_c(a.y, b.y)), ignore, bottom);
		deviceRects[i] = BRect(left, top, right - 1, bottom - 1);
		touched = touched | deviceRects[i];
	}

	DrawTransaction transaction(this, fPainter->ClipRect(touched));
	if (!transaction.IsDirty())
		return;

	rgb_color oldColor = fPainter->HighColor();
	struct pattern pattern = fPainter->Pattern();
	for (int32 i = 0; i < info.lineCount; i++) {
		if (deviceRects[i].IsValid()) {
			fPainter->FillDeviceRect(deviceRects[i], lines[i].color);
			continue;
		}
		// not along an axis: as it is
		fPainter->SetHighColor(lines[i].color);
		fPainter->SetPattern(B_SOLID_HIGH);
		fPainter->StrokeLine(lines[i].startPoint, lines[i].endPoint);
	}
	fPainter->SetHighColor(oldColor);
	fPainter->SetPattern(pattern);
}


// #pragma mark -


BPoint
DrawingEngine::DrawString(const char* string, int32 length,
	const BPoint& pt, escapement_delta* delta)
{
	ASSERT_PARALLEL_LOCKED();

	BPoint penLocation = pt;

	// try a fast clipping path
	if (fPainter->ClippingRegion() != NULL
		&& fPainter->Font().Rotation() == 0.0f
		&& fPainter->IsIdentityTransform()) {
		float fontSize = fPainter->Font().Size();
		BRect clippingFrame = fPainter->ClippingRegion()->Frame();
		// the clipping and the font size are in buffer pixels
		BPoint devicePt(pt.x * fPainter->DeviceScale(),
			pt.y * fPainter->DeviceScale());
		if (devicePt.x - fontSize > clippingFrame.right
			|| devicePt.y + fontSize < clippingFrame.top
			|| devicePt.y - fontSize > clippingFrame.bottom) {
			penLocation.x += StringWidth(string, length, delta);
			return penLocation;
		}
	}

	// use a FontCacheRefernece to speed up the second pass of
	// drawing the string
	FontCacheReference cacheReference;

//bigtime_t now = system_time();
// TODO: BoundingBox is quite slow!! Optimizing it will be beneficial.
// Cursiously, the DrawString after it is actually faster!?!
// TODO: make the availability of the hardware cursor part of the
// HW acceleration flags and skip all calculations for HideFloatingOverlays
// in case we don't have one.
// TODO: Watch out about penLocation and use Painter::PenLocation() when
// not using BoundindBox anymore.
	BRect b = fPainter->BoundingBox(string, length, pt, &penLocation, delta,
		&cacheReference);
	// stop here if we're supposed to render outside of the clipping
	DrawTransaction transaction(this, fPainter->ClipRect(b));
	if (transaction.IsDirty()) {
//printf("bounding box '%s': %lld µs\n", string, system_time() - now);

//now = system_time();
		fPainter->DrawString(string, length, pt, delta, &cacheReference);
//printf("drawing string: %lld µs\n", system_time() - now);
	}

	return penLocation;
}


BPoint
DrawingEngine::DrawString(const char* string, int32 length,
	const BPoint* offsets)
{
	ASSERT_PARALLEL_LOCKED();

	// use a FontCacheReference to speed up the second pass of
	// drawing the string
	FontCacheReference cacheReference;

	BPoint penLocation;
	BRect b = fPainter->BoundingBox(string, length, offsets, &penLocation,
		&cacheReference);
	// stop here if we're supposed to render outside of the clipping
	DrawTransaction transaction(this, fPainter->ClipRect(b));
	if (transaction.IsDirty()) {
//printf("bounding box '%s': %lld µs\n", string, system_time() - now);

//now = system_time();
		fPainter->DrawString(string, length, offsets, &cacheReference);
//printf("drawing string: %lld µs\n", system_time() - now);
	}

	return penLocation;
}


float
DrawingEngine::StringWidth(const char* string, int32 length,
	escapement_delta* delta)
{
	return fPainter->StringWidth(string, length, delta);
}


float
DrawingEngine::StringWidth(const char* string, int32 length,
	const ServerFont& font, escapement_delta* delta)
{
	return font.StringWidth(string, length, delta);
}


BPoint
DrawingEngine::DrawStringDry(const char* string, int32 length,
	const BPoint& pt, escapement_delta* delta)
{
	ASSERT_PARALLEL_LOCKED();

	BPoint penLocation = pt;

	// try a fast path first
	if (fPainter->Font().Rotation() == 0.0f
		&& fPainter->IsIdentityTransform()) {
		penLocation.x += StringWidth(string, length, delta);
		return penLocation;
	}

	fPainter->BoundingBox(string, length, pt, &penLocation, delta, NULL);

	return penLocation;
}


BPoint
DrawingEngine::DrawStringDry(const char* string, int32 length,
	const BPoint* offsets)
{
	ASSERT_PARALLEL_LOCKED();

	BPoint penLocation;
	fPainter->BoundingBox(string, length, offsets, &penLocation, NULL);

	return penLocation;
}


// #pragma mark -


ServerBitmap*
DrawingEngine::DumpToBitmap()
{
	return NULL;
}


status_t
DrawingEngine::ReadBitmap(ServerBitmap* bitmap, bool drawCursor, BRect bounds)
{
	ASSERT_EXCLUSIVE_LOCKED();

	float scale = fPainter->DeviceScale();
	// At native density direct windows may update only the front buffer.
	// At a higher density they are disconnected (see HandleDirectConnection),
	// or, with B_DIRECT_DEVICE_PIXELS, keep the drawing buffer up to date
	// themselves, so it holds the complete desktop. Read its RAM copy
	// instead of pulling the high-resolution frame back across the GPU bus.
	RenderingBuffer* buffer = scale != 1
		? fGraphicsCard->DrawingBuffer() : fGraphicsCard->FrontBuffer();
	if (buffer == NULL)
		return B_ERROR;

	if (scale != 1) {
		// The screen has more pixels than the caller's bitmap: average each
		// block of them into one, which is what a screenshot of a HiDPI
		// screen at its logical size should look like.
		BRect logicalClip(0, 0, roundf(buffer->Width() / scale) - 1,
			roundf(buffer->Height() / scale) - 1);
		bounds = bounds & logicalClip;
		BRect deviceBounds(floorf(bounds.left * scale),
			floorf(bounds.top * scale),
			floorf((bounds.right + 1) * scale) - 1,
			floorf((bounds.bottom + 1) * scale) - 1);
		deviceBounds = deviceBounds
			& BRect(0, 0, buffer->Width() - 1, buffer->Height() - 1);
		AutoFloatingOverlaysHider _(fGraphicsCard, deviceBounds);
		// direct windows that write the frame buffer only (and say so)
		fGraphicsCard->RefreshStale(BRegion(deviceBounds));

		int32 width = bounds.IntegerWidth() + 1;
		int32 height = bounds.IntegerHeight() + 1;

		// The drawing buffer is in memory: average its pixels straight into
		// the caller's bitmap. This runs with the engine locked, and every
		// window waits meanwhile; copying the buffer first and averaging
		// pixel by pixel took a quarter of a second for 3840x1080 at 200%.
		color_space bufferSpace = buffer->ColorSpace();
		if (bufferSpace == B_RGB32 || bufferSpace == B_RGBA32) {
			ScaledReadbackPixels source = {
				(const uint8*)buffer->Bits(), buffer->BytesPerRow() };
			ServerCursorReference cursorRef;
			ScaledReadbackCursor cursorInfo;
			const ScaledReadbackCursor* cursorPointer = NULL;
			if (drawCursor) {
				cursorRef = fGraphicsCard->Cursor();
				ServerCursor* cursor = cursorRef.Get();
				if (cursor != NULL) {
					BPoint position = fGraphicsCard->CursorPosition();
					cursorInfo.bits = (const uint8*)cursor->Bits();
					cursorInfo.width = cursor->Width();
					cursorInfo.height = cursor->Height();
					cursorInfo.left = (int32)(floorf(position.x * scale)
						- cursor->GetHotSpot().x);
					cursorInfo.top = (int32)(floorf(position.y * scale)
						- cursor->GetHotSpot().y);
					cursorPointer = &cursorInfo;
				}
			}

			color_space space = bitmap->ColorSpace();
			if ((space == B_RGB32 || space == B_RGBA32)
				&& bitmap->Bits() != NULL) {
				return scaled_readback(source, scale, bounds.left, bounds.top,
					min_c(width, bitmap->Width()),
					min_c(height, bitmap->Height()),
					(int32)deviceBounds.left, (int32)deviceBounds.top,
					(int32)deviceBounds.right, (int32)deviceBounds.bottom,
					cursorPointer, bitmap->Bits(), bitmap->BytesPerRow());
			}

			// Another format: converted from 32 bits by ImportBits().
			BBitmap logical(BRect(0, 0, width - 1, height - 1),
				B_BITMAP_NO_SERVER_LINK, B_RGB32);
			if (logical.InitCheck() != B_OK)
				return logical.InitCheck();
			status_t result = scaled_readback(source, scale, bounds.left,
				bounds.top, width, height, (int32)deviceBounds.left,
				(int32)deviceBounds.top, (int32)deviceBounds.right,
				(int32)deviceBounds.bottom, cursorPointer,
				(uint8*)logical.Bits(), logical.BytesPerRow());
			if (result != B_OK)
				return result;
			return bitmap->ImportBits(logical.Bits(), logical.BitsLength(),
				logical.BytesPerRow(), logical.ColorSpace(), BPoint(0, 0),
				BPoint(0, 0), width, height);
		}

		// Other frame buffer formats: a converted copy first.
		int32 deviceWidth = deviceBounds.IntegerWidth() + 1;
		int32 deviceHeight = deviceBounds.IntegerHeight() + 1;
		BBitmap device(BRect(0, 0, deviceWidth - 1, deviceHeight - 1),
			B_BITMAP_NO_SERVER_LINK, B_RGB32);
		if (device.InitCheck() != B_OK)
			return device.InitCheck();
		status_t result = device.ImportBits(buffer->Bits(),
			buffer->BitsLength(), buffer->BytesPerRow(), buffer->ColorSpace(),
			deviceBounds.LeftTop(), BPoint(0, 0),
			BSize(deviceWidth - 1, deviceHeight - 1));
		if (result != B_OK)
			return result;

		if (drawCursor) {
			ServerCursorReference cursorRef = fGraphicsCard->Cursor();
			ServerCursor* cursor = cursorRef.Get();
			if (cursor != NULL) {
				BPoint position = fGraphicsCard->CursorPosition();
				position.x = floorf(position.x * scale) - deviceBounds.left
					- cursor->GetHotSpot().x;
				position.y = floorf(position.y * scale) - deviceBounds.top
					- cursor->GetHotSpot().y;
				uint8* bits = (uint8*)device.Bits();
				uint32 bpr = device.BytesPerRow();
				const uint8* cursorBits = (const uint8*)cursor->Bits();
				for (int32 y = 0; y < cursor->Height(); y++) {
					int32 dy = (int32)position.y + y;
					for (int32 x = 0; x < cursor->Width(); x++, cursorBits += 4) {
						int32 dx = (int32)position.x + x;
						if (dx < 0 || dy < 0 || dx >= deviceWidth
							|| dy >= deviceHeight)
							continue;
						uint8* d = bits + dy * bpr + dx * 4;
						uint8 alpha = 255 - cursorBits[3];
						d[0] = ((d[0] * alpha) >> 8) + cursorBits[0];
						d[1] = ((d[1] * alpha) >> 8) + cursorBits[1];
						d[2] = ((d[2] * alpha) >> 8) + cursorBits[2];
					}
				}
			}
		}

		BBitmap logical(BRect(0, 0, width - 1, height - 1),
			B_BITMAP_NO_SERVER_LINK, B_RGB32);
		if (logical.InitCheck() != B_OK)
			return logical.InitCheck();
		const uint8* src = (const uint8*)device.Bits();
		uint32 srcBPR = device.BytesPerRow();
		uint8* dst = (uint8*)logical.Bits();
		uint32 dstBPR = logical.BytesPerRow();
		for (int32 y = 0; y < height; y++) {
			// the buffer rows this logical row covers, as the Painter maps them
			int32 y0 = (int32)floorf((bounds.top + y) * scale)
				- (int32)deviceBounds.top;
			int32 y1 = (int32)floorf((bounds.top + y + 1) * scale) - 1
				- (int32)deviceBounds.top;
			y0 = max_c(0, y0);
			y1 = min_c(deviceHeight - 1, max_c(y0, y1));
			uint8* d = dst + y * dstBPR;
			for (int32 x = 0; x < width; x++, d += 4) {
				int32 x0 = (int32)floorf((bounds.left + x) * scale)
					- (int32)deviceBounds.left;
				int32 x1 = (int32)floorf((bounds.left + x + 1) * scale) - 1
					- (int32)deviceBounds.left;
				x0 = max_c(0, x0);
				x1 = min_c(deviceWidth - 1, max_c(x0, x1));
				uint32 sum[3] = {0, 0, 0};
				uint32 samples = 0;
				for (int32 sy = y0; sy <= y1; sy++) {
					const uint8* s = src + sy * srcBPR + x0 * 4;
					for (int32 sx = x0; sx <= x1; sx++, s += 4) {
						sum[0] += s[0];
						sum[1] += s[1];
						sum[2] += s[2];
						samples++;
					}
				}
				if (samples == 0)
					samples = 1;
				d[0] = sum[0] / samples;
				d[1] = sum[1] / samples;
				d[2] = sum[2] / samples;
				d[3] = 255;
			}
		}
		return bitmap->ImportBits(logical.Bits(), logical.BitsLength(),
			logical.BytesPerRow(), logical.ColorSpace(), BPoint(0, 0),
			BPoint(0, 0), width, height);
	}

	BRect clip(0, 0, buffer->Width() - 1, buffer->Height() - 1);
	bounds = bounds & clip;
	AutoFloatingOverlaysHider _(fGraphicsCard, bounds);

	status_t result = bitmap->ImportBits(buffer->Bits(), buffer->BitsLength(),
		buffer->BytesPerRow(), buffer->ColorSpace(),
		bounds.LeftTop(), BPoint(0, 0),
		bounds.IntegerWidth() + 1, bounds.IntegerHeight() + 1);

	if (drawCursor) {
		ServerCursorReference cursorRef = fGraphicsCard->Cursor();
		ServerCursor* cursor = cursorRef.Get();
		if (!cursor)
			return result;
		int32 cursorWidth = cursor->Width();
		int32 cursorHeight = cursor->Height();

		BPoint cursorPosition = fGraphicsCard->CursorPosition();
		cursorPosition -= bounds.LeftTop() + cursor->GetHotSpot();

		BBitmap cursorArea(BRect(0, 0, cursorWidth - 1, cursorHeight - 1),
			B_BITMAP_NO_SERVER_LINK, B_RGBA32);

		cursorArea.ImportBits(bitmap->Bits(), bitmap->BitsLength(),
			bitmap->BytesPerRow(), bitmap->ColorSpace(),
			cursorPosition,	BPoint(0, 0),
			cursorArea.Bounds().Size());

		uint8* bits = (uint8*)cursorArea.Bits();
		uint8* cursorBits = (uint8*)cursor->Bits();
		for (int32 i = 0; i < cursorHeight; i++) {
			for (int32 j = 0; j < cursorWidth; j++) {
				uint8 alpha = 255 - cursorBits[3];
				bits[0] = ((bits[0] * alpha) >> 8) + cursorBits[0];
				bits[1] = ((bits[1] * alpha) >> 8) + cursorBits[1];
				bits[2] = ((bits[2] * alpha) >> 8) + cursorBits[2];
				cursorBits += 4;
				bits += 4;
			}
		}

		bitmap->ImportBits(cursorArea.Bits(), cursorArea.BitsLength(),
			cursorArea.BytesPerRow(), cursorArea.ColorSpace(),
			BPoint(0, 0), cursorPosition,
			cursorWidth, cursorHeight);
	}

	return result;
}


// #pragma mark -


BRect
DrawingEngine::CopyRect(BRect src, int32 xOffset, int32 yOffset) const
{
	// TODO: assumes drawing buffer is 32 bits (which it currently always is)
	BRect dst;
	RenderingBuffer* buffer = fGraphicsCard->DrawingBuffer();
	if (buffer) {
		BRect clip(0, 0, buffer->Width() - 1, buffer->Height() - 1);

		dst = src;
		dst.OffsetBy(xOffset, yOffset);

		if (clip.Intersects(src) && clip.Intersects(dst)) {
			uint32 bytesPerRow = buffer->BytesPerRow();
			uint8* bits = (uint8*)buffer->Bits();

			// clip source rect
			src = src & clip;
			// clip dest rect
			dst = dst & clip;
			// move dest back over source and clip source to dest
			dst.OffsetBy(-xOffset, -yOffset);
			src = src & dst;

			// calc offset in buffer
			bits += (ssize_t)src.left * 4 + (ssize_t)src.top * bytesPerRow;

			uint32 width = src.IntegerWidth() + 1;
			uint32 height = src.IntegerHeight() + 1;

			_CopyRect(bits, width, height, bytesPerRow,
				xOffset, yOffset);

			// offset dest again, because it is return value
			dst.OffsetBy(xOffset, yOffset);
		}
	}
	return dst;
}


void
DrawingEngine::SetRendererOffset(int32 offsetX, int32 offsetY)
{
	fPainter->SetRendererOffset(offsetX, offsetY);
}


void
DrawingEngine::_CopyRect(uint8* src, uint32 width, uint32 height,
	uint32 bytesPerRow, int32 xOffset, int32 yOffset) const
{
	// TODO: assumes drawing buffer is 32 bits (which it currently always is)
	int32 yIncrement;
	const bool needMemmove = (yOffset == 0 && xOffset > 0 && uint32(xOffset) <= width);

	if (yOffset > 0) {
		// copy from bottom to top
		yIncrement = -bytesPerRow;
		src += (height - 1) * bytesPerRow;
	} else {
		// copy from top to bottom
		yIncrement = bytesPerRow;
	}

	uint8* dst = src + (ssize_t)yOffset * bytesPerRow + (ssize_t)xOffset * 4;

	if (!needMemmove) {
		for (uint32 y = 0; y < height; y++) {
			memcpy(dst, src, width * 4);
			src += yIncrement;
			dst += yIncrement;
		}
	} else {
		for (uint32 y = 0; y < height; y++) {
			memmove(dst, src, width * 4);
			src += yIncrement;
			dst += yIncrement;
		}
	}
}
