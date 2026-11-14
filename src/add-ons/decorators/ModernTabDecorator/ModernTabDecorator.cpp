/*
 * Copyright 2026, Koan contributors. All rights reserved.
 * Distributed under the terms of the MIT License.
 */


/*!	Window decorator with a tab strip for native window stacks. */


#include "ModernTabDecorator.h"

#include <math.h>
#include <new>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <Point.h>
#include <Rect.h>
#include <Region.h>
#include <View.h>

#include <WindowPrivate.h>

#include "DesktopSettings.h"
#include "DrawingEngine.h"


//#define DEBUG_MODERN_TAB_DECORATOR
#ifdef DEBUG_MODERN_TAB_DECORATOR
#	define STRACE(x) printf x
#else
#	define STRACE(x) ;
#endif


// All sizes are logical units; app_server scales them when it rasterizes.

// window frame
static const int32 kBorderWidth = 2;
static const float kResizeKnobSize = 14.0f;
static const float kBorderResizeLength = 20.0f;

// title bar / tab strip
static const float kMinStripHeight = 32.0f;
static const float kStripTextPadding = 16.0f;
	// added to the font height to get the strip height
static const float kTabTopInset = 5.0f;
static const float kStripEndMargin = 8.0f;
static const float kTabRadius = 6.0f;
static const float kTabPadding = 10.0f;
static const float kTabCloseSize = 16.0f;
static const float kTabCloseRightInset = 6.0f;
static const float kTitleCloseGap = 4.0f;
static const float kAccentHeight = 2.0f;

// tab widths
static const float kMinTabWidth = 72.0f;
static const float kMaxTabWidth = 220.0f;
static const float kTabGap = 2.0f;
static const float kOverflowStubWidth = 14.0f;
static const float kCloseButtonMinTabWidth = 96.0f;

// window controls
static const float kControlSize = 12.0f;
static const float kControlSpacing = 8.0f;
static const float kControlsMargin = 10.0f;
static const float kControlsTrailingSpace = 12.0f;

// window control colors, the same in light and dark appearance
static const rgb_color kCloseColor = { 232, 87, 76, 255 };
static const rgb_color kMinimizeColor = { 232, 170, 58, 255 };
static const rgb_color kZoomColor = { 82, 180, 90, 255 };

static inline rgb_color
blend(rgb_color from, rgb_color to, uint8 amount)
{
	// amount 0 gives "from", 255 gives "to"
	rgb_color result;
	result.red = (from.red * (255 - amount) + to.red * amount) / 255;
	result.green = (from.green * (255 - amount) + to.green * amount) / 255;
	result.blue = (from.blue * (255 - amount) + to.blue * amount) / 255;
	result.alpha = 255;
	return result;
}


static inline BRect
invalid_rect()
{
	return BRect(0.0f, 0.0f, -1.0f, -1.0f);
}


// #pragma mark - ModernTabDecorAddOn


ModernTabDecorAddOn::ModernTabDecorAddOn(image_id id, const char* name)
	:
	DecorAddOn(id, name)
{
}


Decorator*
ModernTabDecorAddOn::_AllocateDecorator(DesktopSettings& settings, BRect rect,
	Desktop* desktop)
{
	return new (std::nothrow) ModernTabDecorator(settings, rect, desktop);
}


// #pragma mark - StripTab


ModernTabDecorator::StripTab::StripTab()
	:
	Decorator::Tab(),
	tabCloseRect(invalid_rect()),
	isStub(false),
	closeAlwaysShown(false)
{
}


// #pragma mark - ModernTabDecorator


ModernTabDecorator::ModernTabDecorator(DesktopSettings& settings, BRect frame,
	Desktop* desktop)
	:
	SATDecorator(settings, frame, desktop),
	fSlots(NULL),
	fSlotCapacity(0),
	fFirstVisibleTab(0),
	fStripHeight(kMinStripHeight),
	fCloseHitInTab(false),
	fHoverRegion(REGION_NONE),
	fHoverTab(-1),
	fHoverInTabClose(false),
	fDragTab(-1),
	fDragGrabOffset(0),
	fDragPointerX(0),
	fDraggedLeft(0),
	fDropIndex(-1),
	fDragOffsets(NULL)
{
	fMetrics.minTabWidth = kMinTabWidth;
	fMetrics.maxTabWidth = kMaxTabWidth;
	fMetrics.tabGap = kTabGap;
	fMetrics.overflowStubWidth = kOverflowStubWidth;
	fMetrics.closeButtonMinTabWidth = kCloseButtonMinTabWidth;

	_UpdatePalette(settings);

	STRACE(("ModernTabDecorator: frame (%.1f,%.1f,%.1f,%.1f)\n",
		frame.left, frame.top, frame.right, frame.bottom));
}


ModernTabDecorator::~ModernTabDecorator()
{
	free(fSlots);
	free(fDragOffsets);
}


// #pragma mark - public


void
ModernTabDecorator::Draw(BRect updateRect)
{
	fDrawingEngine->SetDrawState(&fDrawState);

	_DrawTabs(updateRect & fTitleBarRect);
	_DrawFrame(updateRect & fBorderRect);
}


void
ModernTabDecorator::Draw()
{
	fDrawingEngine->SetDrawState(&fDrawState);

	_DrawTabs(fTitleBarRect);
	_DrawFrame(fBorderRect);
}


Decorator::Region
ModernTabDecorator::RegionAt(BPoint where, int32& tabIndex) const
{
	tabIndex = -1;

	{
		AutoReadLocker _(fLocker);

		if (fTitleBarRect.IsValid() && fTitleBarRect.Contains(where)) {
			int32 topIndex = fTabList.IndexOf(fTopTab);
			if (fTopTab != NULL) {
				if (fTopTab->closeRect.Contains(where)) {
					tabIndex = topIndex;
					fCloseHitInTab = false;
					return REGION_CLOSE_BUTTON;
				}
				if (fTopTab->minimizeRect.Contains(where)) {
					tabIndex = topIndex;
					return REGION_MINIMIZE_BUTTON;
				}
				if (fTopTab->zoomRect.Contains(where)) {
					tabIndex = topIndex;
					return REGION_ZOOM_BUTTON;
				}
			}

			for (int32 i = 0; i < fTabList.CountItems(); i++) {
				StripTab* tab = _StripTabAt(i);
				if (_IsCloseShown(tab) && tab->tabCloseRect.Contains(where)) {
					tabIndex = i;
					fCloseHitInTab = true;
					return REGION_CLOSE_BUTTON;
				}
				if (tab->tabRect.Contains(where)) {
					tabIndex = i;
					return REGION_TAB;
				}
			}

			// The empty part of the strip moves the window (or its group),
			// like the classic top border.
			return REGION_TOP_BORDER;
		}
	}

	// borders and resize corners
	return SATDecorator::RegionAt(where, tabIndex);
}


bool
ModernTabDecorator::SetRegionHighlight(Region region, uint8 highlight,
	BRegion* dirty, int32 tab)
{
	if (highlight != HIGHLIGHT_HOVER)
		return SATDecorator::SetRegionHighlight(region, highlight, dirty, tab);

	// The hover state: one region of one tab at a time. Only what the strip
	// draws differently counts; the borders have no hover look.
	AutoWriteLocker _(fLocker);

	switch (region) {
		case REGION_TAB:
		case REGION_CLOSE_BUTTON:
		case REGION_MINIMIZE_BUTTON:
		case REGION_ZOOM_BUTTON:
			if (_StripTabAt(tab) != NULL)
				break;
			// fall through
		default:
			region = REGION_NONE;
			tab = -1;
			break;
	}

	bool inTabClose = region == REGION_CLOSE_BUTTON && fCloseHitInTab;
	if (region == fHoverRegion && tab == fHoverTab
		&& inTabClose == fHoverInTabClose) {
		return true;
	}

	// only the tabs and buttons whose look changes are redrawn
	_IncludeHoverRects(dirty, fHoverRegion, fHoverTab);
	fHoverRegion = region;
	fHoverTab = tab;
	fHoverInTabClose = inTabClose;
	_IncludeHoverRects(dirty, fHoverRegion, fHoverTab);
	return true;
}


void
ModernTabDecorator::ExtendDirtyRegion(Region region, BRegion& dirty)
{
	switch (region) {
		case REGION_TAB:
		case REGION_CLOSE_BUTTON:
		case REGION_MINIMIZE_BUTTON:
		case REGION_ZOOM_BUTTON:
		{
			// tab close buttons and highlights live anywhere in the strip
			AutoReadLocker _(fLocker);
			if (fTitleBarRect.IsValid())
				dirty.Include(fTitleBarRect);
			break;
		}

		default:
			SATDecorator::ExtendDirtyRegion(region, dirty);
			break;
	}
}


int32
ModernTabDecorator::SetDraggedTab(int32 tab, float grabOffset, float pointerX,
	BRegion* dirty)
{
	AutoWriteLocker _(fLocker);

	int32 count = fTabList.CountItems();
	bool valid = tab >= 0 && tab < count && fTitleBarRect.IsValid()
		&& fSlots != NULL && count <= fSlotCapacity;
	if (!valid) {
		if (fDragTab >= 0) {
			fDragTab = -1;
			fDropIndex = -1;
			_PlaceTabs();
			if (dirty != NULL) {
				dirty->Include(fTitleBarRect);
				dirty->Include(fTopBorder);
			}
		}
		return tab < 0 ? -1 : tab;
	}

	fDragTab = tab;
	fDragGrabOffset = grabOffset;
	fDragPointerX = pointerX;
	_PlaceTabs();

	// the strip, and the top border the active tab flows into
	if (dirty != NULL) {
		dirty->Include(fTitleBarRect);
		dirty->Include(fTopBorder);
	}
	return fDropIndex;
}


bool
ModernTabDecorator::MoveTab(int32 from, int32 to, bool isMoving,
	BRegion* updateRegion)
{
	// Decorator::MoveTab() reorders the tab list after _MoveTab(); lay out
	// once the list has its new order.
	if (!SATDecorator::MoveTab(from, to, isMoving, updateRegion))
		return false;

	AutoWriteLocker _(fLocker);
	// tab indexes changed: a running drag restarts with the next move
	fDragTab = -1;
	fHoverRegion = REGION_NONE;
	fHoverTab = -1;
	_DoLayout();
	if (updateRegion != NULL && fTitleBarRect.IsValid())
		updateRegion->Include(fTitleBarRect);
	return true;
}


void
ModernTabDecorator::UpdateColors(DesktopSettings& settings)
{
	SATDecorator::UpdateColors(settings);
	_UpdatePalette(settings);
}


// #pragma mark - protected


Decorator::Tab*
ModernTabDecorator::_AllocateNewTab()
{
	StripTab* tab = new (std::nothrow) StripTab;
	if (tab == NULL)
		return NULL;

	_SetFocus(tab);
	return tab;
}


void
ModernTabDecorator::_DoLayout()
{
	STRACE(("ModernTabDecorator::_DoLayout()\n"));

	bool hasStrip = false;
	fBorderWidth = 0;

	if (fTopTab != NULL) {
		switch ((int)fTopTab->look) {
			case B_TITLED_WINDOW_LOOK:
			case B_DOCUMENT_WINDOW_LOOK:
			case B_FLOATING_WINDOW_LOOK:
			case kLeftTitledWindowLook:
				hasStrip = true;
				fBorderWidth = kBorderWidth;
				break;

			case B_MODAL_WINDOW_LOOK:
			case B_BORDERED_WINDOW_LOOK:
				fBorderWidth = kBorderWidth;
				break;

			default:
				break;
		}
	}

	const float bw = fBorderWidth;
	fBorderRect = fFrame.InsetByCopy(-bw, -bw);

	// NOTE: like TabDecorator, the left and right borders exclude the
	// corners, which belong to the top and bottom borders.
	if (fBorderWidth > 0) {
		fLeftBorder.Set(fFrame.left - bw, fFrame.top, fFrame.left - 1,
			fFrame.bottom);
		fRightBorder.Set(fFrame.right + 1, fFrame.top, fFrame.right + bw,
			fFrame.bottom);
		fTopBorder.Set(fFrame.left - bw, fFrame.top - bw, fFrame.right + bw,
			fFrame.top - 1);
		fBottomBorder.Set(fFrame.left - bw, fFrame.bottom + 1,
			fFrame.right + bw, fFrame.bottom + bw);
	} else {
		fLeftBorder = invalid_rect();
		fRightBorder = invalid_rect();
		fTopBorder = invalid_rect();
		fBottomBorder = invalid_rect();
	}

	fBorderResizeLength = kBorderResizeLength;
	fResizeKnobSize = kResizeKnobSize;
	if (fTopTab != NULL && fTopTab->look == B_DOCUMENT_WINDOW_LOOK) {
		fResizeRect.Set(fFrame.right - kResizeKnobSize + 1,
			fFrame.bottom - kResizeKnobSize + 1, fFrame.right, fFrame.bottom);
	} else
		fResizeRect = invalid_rect();

	const int32 count = fTabList.CountItems();
	for (int32 i = 0; i < count; i++) {
		StripTab* tab = _StripTabAt(i);
		tab->tabRect = invalid_rect();
		tab->closeRect = invalid_rect();
		tab->minimizeRect = invalid_rect();
		tab->zoomRect = invalid_rect();
		tab->tabCloseRect = invalid_rect();
		tab->isStub = false;
	}

	if (!hasStrip || count == 0) {
		fTitleBarRect = invalid_rect();
		return;
	}

	fTitleBarRect.Set(fBorderRect.left, fBorderRect.top - fStripHeight,
		fBorderRect.right, fBorderRect.top - 1);

	// window controls at the left, vertically centered
	const float controlTop = roundf(fTitleBarRect.top
		+ (fStripHeight - kControlSize) / 2);
	float x = fTitleBarRect.left + kControlsMargin;
	BRect closeRect(x, controlTop, x + kControlSize - 1,
		controlTop + kControlSize - 1);
	x += kControlSize + kControlSpacing;
	BRect minimizeRect(x, controlTop, x + kControlSize - 1,
		controlTop + kControlSize - 1);
	x += kControlSize + kControlSpacing;
	BRect zoomRect(x, controlTop, x + kControlSize - 1,
		controlTop + kControlSize - 1);
	x += kControlSize;

	// the strip takes the rest: [stripLeft, stripRight)
	const float stripLeft = x + kControlsTrailingSpace;
	const float stripRight = fTitleBarRect.right + 1 - kStripEndMargin;
	const float minTabSize = stripLeft - fTitleBarRect.left + kMinTabWidth
		+ kStripEndMargin;

	const bool haveSlots = _EnsureSlots(count);
	if (haveSlots) {
		fFirstVisibleTab = TabStripLayout::Layout(fMetrics, count, stripLeft,
			stripRight, fTabList.IndexOf(fTopTab), fFirstVisibleTab, fSlots);
	}

	if (fDragTab >= count)
		fDragTab = -1;
	if (fHoverTab >= count) {
		fHoverRegion = REGION_NONE;
		fHoverTab = -1;
	}

	for (int32 i = 0; i < count; i++) {
		StripTab* tab = _StripTabAt(i);
		tab->minTabSize = minTabSize;
		tab->maxTabSize = kMaxTabWidth;
		tab->textOffset = kTabPadding;
		tab->closeAlwaysShown = false;

		if (_IsActive(tab)) {
			tab->closeRect = closeRect;
			tab->minimizeRect = minimizeRect;
			tab->zoomRect = zoomRect;
		}

		tab->truncatedTitle = "";
		tab->truncatedTitleLength = 0;
		if (!haveSlots || !fSlots[i].visible || fSlots[i].stub)
			continue;

		// A tab narrower than the close button rule shows its close button
		// only on hover, over the end of the title.
		const TabStripSlot& slot = fSlots[i];
		tab->closeAlwaysShown = slot.showsCloseButton;
		float titleWidth = slot.right - slot.left - 2 * kTabPadding;
		if (tab->closeAlwaysShown) {
			titleWidth -= kTabCloseSize + kTabCloseRightInset - kTabPadding
				+ kTitleCloseGap;
		}
		if (titleWidth < 0)
			titleWidth = 0;
		tab->truncatedTitle = Title(tab) != NULL ? Title(tab) : "";
		fDrawState.Font().TruncateString(&tab->truncatedTitle, B_TRUNCATE_END,
			titleWidth);
		tab->truncatedTitleLength = tab->truncatedTitle.Length();
	}

	_PlaceTabs();
}


void
ModernTabDecorator::_DrawFrame(BRect invalid)
{
	if (fTopTab == NULL || fBorderWidth <= 0 || !invalid.IsValid())
		return;

	bool focused = _IsFocusedStack();
	rgb_color fill = focused ? fStripColor : fInactiveStripColor;
	rgb_color border = focused ? fBorderColor : fInactiveBorderColor;

	fDrawingEngine->FillRect(fLeftBorder & invalid, fill);
	fDrawingEngine->FillRect(fRightBorder & invalid, fill);
	fDrawingEngine->FillRect(fTopBorder & invalid, fill);
	fDrawingEngine->FillRect(fBottomBorder & invalid, fill);

	// the active tab flows into the window contents
	if (fTitleBarRect.IsValid() && fTopTab->tabRect.IsValid()
		&& !static_cast<StripTab*>(fTopTab)->isStub) {
		BRect below(fTopTab->tabRect.left, fTopBorder.top,
			fTopTab->tabRect.right, fTopBorder.bottom);
		fDrawingEngine->FillRect(below & invalid, fSurfaceColor);
	}

	// one outline around title bar and frame
	BRect outline = fBorderRect;
	if (fTitleBarRect.IsValid())
		outline.top = fTitleBarRect.top;
	fDrawingEngine->StrokeRect(outline, border);
}


void
ModernTabDecorator::_DrawTabs(BRect invalid)
{
	if (!fTitleBarRect.IsValid() || !invalid.IsValid())
		return;

	_DrawStripBackground(invalid);

	// the active tab goes over the others, a dragged tab over everything
	Decorator::Tab* active = NULL;
	Decorator::Tab* dragged = fTabList.ItemAt(fDragTab);
	for (int32 i = 0; i < fTabList.CountItems(); i++) {
		Decorator::Tab* tab = fTabList.ItemAt(i);
		if (_IsActive(tab)) {
			active = tab;
			continue;
		}
		if (tab != dragged && invalid.Intersects(tab->tabRect))
			_DrawTab(tab, invalid);
	}
	if (active != NULL && active != dragged
		&& invalid.Intersects(active->tabRect)) {
		_DrawTab(active, invalid);
	}
	if (dragged != NULL && invalid.Intersects(dragged->tabRect))
		_DrawTab(dragged, invalid);

	if (fTopTab != NULL)
		_DrawButtons(fTopTab, invalid);
}


void
ModernTabDecorator::_DrawTab(Decorator::Tab* _tab, BRect invalid)
{
	StripTab* tab = static_cast<StripTab*>(_tab);
	if (!tab->tabRect.IsValid() || !fTitleBarRect.IsValid())
		return;

	bool focused = _IsFocusedStack();
	BRect rect = tab->tabRect;
	int32 index = fTabList.IndexOf(tab);

	// start from the strip, this may be a lone redraw of one tab (but not
	// while dragging: tabs overlap then, and the strip was just drawn)
	if (fDragTab < 0) {
		_DrawStripBackground(BRect(rect.left, fTitleBarRect.top, rect.right,
			fTitleBarRect.bottom));
	}

	if (tab->isStub)
		_DrawStub(tab);
	else
		_DrawTabSurface(tab, rect, _IsActive(tab), focused, _IsHovered(index));

	if (tab->isHighlighted) {
		// candidate for Stack & Tile: outline the tab
		fDrawingEngine->SetHighColor(fHighlightColor);
		fDrawingEngine->DrawRoundRect(rect, kTabRadius, kTabRadius, false);
	}

	if (!tab->isStub) {
		_DrawTitle(tab, rect);
		_DrawTabCloseButton(tab);
	}

	_DrawSeparatorAfter(index - 1);
	_DrawSeparatorAfter(index);

	if (_IsActive(tab))
		_DrawButtons(tab, invalid);
}


void
ModernTabDecorator::_DrawTitle(Decorator::Tab* tab, BRect rect)
{
	if (tab->truncatedTitleLength <= 0 || !rect.IsValid())
		return;

	bool focused = _IsFocusedStack();
	bool active = _IsActive(tab);
	rgb_color background = active ? fSurfaceColor
		: (focused ? fStripColor : fInactiveStripColor);
	if (!active && _IsHovered(fTabList.IndexOf(tab)))
		background = blend(background, fTextColor, 16);
	rgb_color text = active && focused ? fTextColor : fInactiveTextColor;

	font_height fontHeight;
	fDrawState.Font().GetHeight(fontHeight);

	BPoint titlePos;
	titlePos.x = rect.left + tab->textOffset;
	titlePos.y = floorf((rect.top + rect.bottom + fontHeight.ascent
		- fontHeight.descent) / 2.0f + 0.5f);

	fDrawingEngine->SetFont(fDrawState.Font());
	fDrawingEngine->SetHighColor(text);
	fDrawingEngine->SetLowColor(background);
	fDrawingEngine->SetDrawingMode(B_OP_OVER);
	fDrawingEngine->DrawString(tab->truncatedTitle.String(),
		tab->truncatedTitleLength, titlePos);
	fDrawingEngine->SetDrawingMode(B_OP_COPY);
}


void
ModernTabDecorator::_DrawButtons(Decorator::Tab* tab, const BRect& invalid)
{
	if (!_IsActive(tab))
		return;

	int32 index = fTabList.IndexOf(tab);
	bool hoveredTab = fHoverTab == index;

	if (invalid.Intersects(tab->closeRect)) {
		_DrawControl(tab, tab->closeRect, fCloseColor, GLYPH_CLOSE,
			tab->closePressed && !fCloseHitInTab,
			(tab->flags & B_NOT_CLOSABLE) == 0,
			hoveredTab && fHoverRegion == REGION_CLOSE_BUTTON
				&& !fHoverInTabClose);
	}
	if (invalid.Intersects(tab->minimizeRect)) {
		_DrawControl(tab, tab->minimizeRect, fMinimizeColor, GLYPH_MINIMIZE,
			tab->minimizePressed, (tab->flags & B_NOT_MINIMIZABLE) == 0,
			hoveredTab && fHoverRegion == REGION_MINIMIZE_BUTTON);
	}
	if (invalid.Intersects(tab->zoomRect)) {
		_DrawControl(tab, tab->zoomRect, fZoomColor, GLYPH_ZOOM,
			tab->zoomPressed, (tab->flags & B_NOT_ZOOMABLE) == 0,
			hoveredTab && fHoverRegion == REGION_ZOOM_BUTTON);
	}
}


void
ModernTabDecorator::_DrawClose(Decorator::Tab* tab, bool direct, BRect rect)
{
	// The pressed state belongs to either the window control or the tab's
	// own close button; redrawing the tab covers both.
	if (fTitleBarRect.IsValid())
		_DrawTab(tab, fTitleBarRect);
}


void
ModernTabDecorator::_DrawMinimize(Decorator::Tab* tab, bool direct,
	BRect rect)
{
	_DrawButtons(tab, tab->minimizeRect);
}


void
ModernTabDecorator::_DrawZoom(Decorator::Tab* tab, bool direct, BRect rect)
{
	_DrawButtons(tab, tab->zoomRect);
}


void
ModernTabDecorator::_SetTitle(Decorator::Tab* tab, const char* string,
	BRegion* updateRegion)
{
	_DoLayout();

	if (updateRegion != NULL && fTitleBarRect.IsValid())
		updateRegion->Include(fTitleBarRect);
}


void
ModernTabDecorator::_SetFocus(Decorator::Tab* tab)
{
	// Colors are chosen while drawing, from the tab's focus state.
}


bool
ModernTabDecorator::_SetTabLocation(Decorator::Tab* tab, float location,
	bool isShifting, BRegion* updateRegion)
{
	// The strip places tabs itself; free tab positions are not supported.
	return false;
}


void
ModernTabDecorator::_MoveBy(BPoint offset)
{
	for (int32 i = 0; i < fTabList.CountItems(); i++) {
		StripTab* tab = _StripTabAt(i);
		tab->tabRect.OffsetBy(offset);
		tab->closeRect.OffsetBy(offset);
		tab->minimizeRect.OffsetBy(offset);
		tab->zoomRect.OffsetBy(offset);
		tab->tabCloseRect.OffsetBy(offset);
	}

	if (fSlots != NULL) {
		int32 count = min_c(fTabList.CountItems(), fSlotCapacity);
		for (int32 i = 0; i < count; i++) {
			fSlots[i].left += offset.x;
			fSlots[i].right += offset.x;
		}
	}
	fDraggedLeft += offset.x;
	fDragPointerX += offset.x;

	fFrame.OffsetBy(offset);
	fTitleBarRect.OffsetBy(offset);
	fBorderRect.OffsetBy(offset);
	fResizeRect.OffsetBy(offset);
	fLeftBorder.OffsetBy(offset);
	fRightBorder.OffsetBy(offset);
	fTopBorder.OffsetBy(offset);
	fBottomBorder.OffsetBy(offset);
}


void
ModernTabDecorator::_ResizeBy(BPoint offset, BRegion* dirty)
{
	// The tab widths depend on the window width: the whole strip and the
	// borders, before and after, need a redraw. The contents are not ours.
	BRegion oldDecoration;
	if (dirty != NULL)
		_GetFootprint(&oldDecoration);

	fFrame.right += offset.x;
	fFrame.bottom += offset.y;

	_DoLayout();

	if (dirty != NULL) {
		BRegion newDecoration;
		_GetFootprint(&newDecoration);
		oldDecoration.Exclude(fFrame);
		dirty->Include(&oldDecoration);
		dirty->Include(&newDecoration);
	}
}


bool
ModernTabDecorator::_AddTab(DesktopSettings& settings, int32 index,
	BRegion* updateRegion)
{
	// tab indexes change: drop index based state
	fDragTab = -1;
	fHoverRegion = REGION_NONE;
	fHoverTab = -1;

	_UpdateFont(settings);
	_DoLayout();
	_DoOutlineLayout();

	if (updateRegion != NULL && fTitleBarRect.IsValid())
		updateRegion->Include(fTitleBarRect);
	return true;
}


bool
ModernTabDecorator::_RemoveTab(int32 index, BRegion* updateRegion)
{
	BRect oldTitleBar = fTitleBarRect;

	// tab indexes change: drop index based state
	fDragTab = -1;
	fHoverRegion = REGION_NONE;
	fHoverTab = -1;

	_DoLayout();
	_DoOutlineLayout();

	if (updateRegion != NULL) {
		if (oldTitleBar.IsValid())
			updateRegion->Include(oldTitleBar);
		if (fTitleBarRect.IsValid())
			updateRegion->Include(fTitleBarRect);
	}
	return true;
}


bool
ModernTabDecorator::_MoveTab(int32 from, int32 to, bool isMoving,
	BRegion* updateRegion)
{
	// Called before the tab list is reordered; MoveTab() lays out after.
	int32 count = fTabList.CountItems();
	return from >= 0 && from < count && to >= 0 && to < count;
}


void
ModernTabDecorator::_GetFootprint(BRegion* region)
{
	if (region == NULL)
		return;

	region->MakeEmpty();

	if (fTopTab == NULL || fTopTab->look == B_NO_BORDER_WINDOW_LOOK)
		return;

	if (fBorderWidth > 0) {
		region->Set(fBorderRect);
		region->Exclude(fFrame);
	}

	if (fTitleBarRect.IsValid())
		region->Include(fTitleBarRect);
}


void
ModernTabDecorator::_UpdateFont(DesktopSettings& settings)
{
	ServerFont font;
	settings.GetDefaultPlainFont(font);
	font.SetFlags(B_FORCE_ANTIALIASING);
	font.SetSpacing(B_STRING_SPACING);
	fDrawState.SetFont(font);

	font_height fontHeight;
	font.GetHeight(fontHeight);
	float height = ceilf(fontHeight.ascent + fontHeight.descent
		+ fontHeight.leading) + kStripTextPadding;
	fStripHeight = max_c(kMinStripHeight, height);
}


void
ModernTabDecorator::_TopTabChanged()
{
	// The active tab must be visible when the strip overflows, and it
	// carries the window controls.
	_DoLayout();
}


// #pragma mark - protected helpers


ModernTabDecorator::StripTab*
ModernTabDecorator::_StripTabAt(int32 index) const
{
	return static_cast<StripTab*>(fTabList.ItemAt(index));
}


// #pragma mark - private


bool
ModernTabDecorator::_EnsureSlots(int32 count)
{
	if (count <= fSlotCapacity)
		return fSlots != NULL;

	// grow in steps, layout runs on every resize
	int32 capacity = max_c(count, max_c(8, fSlotCapacity * 2));
	float* offsets = (float*)realloc(fDragOffsets, capacity * sizeof(float));
	if (offsets == NULL)
		return fSlots != NULL && count <= fSlotCapacity;
	fDragOffsets = offsets;

	TabStripSlot* slots = (TabStripSlot*)realloc(fSlots,
		capacity * sizeof(TabStripSlot));
	if (slots == NULL)
		return false;

	fSlots = slots;
	fSlotCapacity = capacity;
	return true;
}


/*!	Places the tab rectangles from the slots, shifted by a running tab drag:
	the dragged tab under the pointer, the others making room. Only the
	positions change, so this is cheap enough for every pointer move.
*/
void
ModernTabDecorator::_PlaceTabs()
{
	const int32 count = fTabList.CountItems();
	if (!fTitleBarRect.IsValid() || fSlots == NULL || count > fSlotCapacity)
		return;

	bool dragging = fDragTab >= 0 && fDragTab < count;
	if (dragging) {
		fDropIndex = TabStripLayout::DragLayout(fSlots, count, fDragTab,
			fDragGrabOffset, fDragPointerX, &fDraggedLeft, fDragOffsets);
	} else
		fDropIndex = -1;

	const float tabTop = fTitleBarRect.top + kTabTopInset;
	for (int32 i = 0; i < count; i++) {
		StripTab* tab = _StripTabAt(i);
		const TabStripSlot& slot = fSlots[i];
		if (!slot.visible) {
			tab->tabRect = invalid_rect();
			tab->tabCloseRect = invalid_rect();
			tab->isStub = false;
			continue;
		}

		float left = slot.left;
		float right = slot.right;
		if (dragging && i == fDragTab) {
			right = fDraggedLeft + (right - left);
			left = fDraggedLeft;
		} else if (dragging && fDragOffsets != NULL) {
			left += fDragOffsets[i];
			right += fDragOffsets[i];
		}

		tab->tabRect.Set(left, tabTop, right - 1, fTitleBarRect.bottom);
		tab->tabOffset = (uint32)(slot.left - fTitleBarRect.left);
		tab->isStub = slot.stub;

		if (!slot.stub
			&& right - left >= 2 * kTabPadding + kTabCloseSize) {
			float closeTop = roundf(tabTop
				+ (fTitleBarRect.bottom - tabTop + 1 - kTabCloseSize) / 2);
			float closeRight = tab->tabRect.right - kTabCloseRightInset;
			tab->tabCloseRect.Set(closeRight - kTabCloseSize + 1, closeTop,
				closeRight, closeTop + kTabCloseSize - 1);
		} else
			tab->tabCloseRect = invalid_rect();
	}
}


void
ModernTabDecorator::_IncludeHoverRects(BRegion* dirty, Region region,
	int32 index) const
{
	StripTab* tab = _StripTabAt(index);
	if (dirty == NULL || tab == NULL)
		return;

	switch (region) {
		case REGION_TAB:
			if (tab->tabRect.IsValid())
				dirty->Include(tab->tabRect);
			break;

		case REGION_CLOSE_BUTTON:
			// either the window control or the tab's own button
			if (tab->closeRect.IsValid())
				dirty->Include(tab->closeRect);
			if (tab->tabRect.IsValid())
				dirty->Include(tab->tabRect);
			break;

		case REGION_MINIMIZE_BUTTON:
			if (tab->minimizeRect.IsValid())
				dirty->Include(tab->minimizeRect);
			break;

		case REGION_ZOOM_BUTTON:
			if (tab->zoomRect.IsValid())
				dirty->Include(tab->zoomRect);
			break;

		default:
			break;
	}
}


//! Whether the pointer is over the tab at \a index (or its close button).
bool
ModernTabDecorator::_IsHovered(int32 index) const
{
	if (index < 0 || index != fHoverTab || fDragTab >= 0)
		return false;
	return fHoverRegion == REGION_TAB
		|| (fHoverRegion == REGION_CLOSE_BUTTON && fHoverInTabClose);
}


bool
ModernTabDecorator::_IsCloseShown(const StripTab* tab) const
{
	if (!tab->tabCloseRect.IsValid() || tab->isStub)
		return false;
	return tab->closeAlwaysShown || _IsHovered(fTabList.IndexOf(tab));
}


void
ModernTabDecorator::_UpdatePalette(DesktopSettings& settings)
{
	rgb_color panel = settings.UIColor(B_PANEL_BACKGROUND_COLOR);
	rgb_color text = settings.UIColor(B_PANEL_TEXT_COLOR);
	bool dark = panel.IsDark();

	// The active tab has the color of a window's contents; the strip is a
	// step away from it, toward the text color, which works for light and
	// dark appearances alike.
	fSurfaceColor = panel;
	fStripColor = blend(panel, text, dark ? 30 : 22);
	fInactiveStripColor = blend(panel, text, dark ? 16 : 10);
	fTextColor = text;
	fInactiveTextColor = blend(text, fStripColor, 110);
	fSeparatorColor = blend(fStripColor, text, 50);

	fBorderColor = settings.UIColor(B_WINDOW_BORDER_COLOR);
	fInactiveBorderColor = settings.UIColor(B_WINDOW_INACTIVE_BORDER_COLOR);
	fAccentColor = settings.UIColor(B_WINDOW_TAB_COLOR);
	fHighlightColor = settings.UIColor(B_CONTROL_HIGHLIGHT_COLOR);

	fCloseColor = kCloseColor;
	fMinimizeColor = kMinimizeColor;
	fZoomColor = kZoomColor;
}


void
ModernTabDecorator::_DrawStripBackground(BRect invalid)
{
	bool focused = _IsFocusedStack();
	fDrawingEngine->FillRect(fTitleBarRect & invalid,
		focused ? fStripColor : fInactiveStripColor);

	// a hairline separates the strip from the window contents; the active
	// tab covers it
	fDrawingEngine->StrokeLine(
		BPoint(fTitleBarRect.left, fTitleBarRect.bottom),
		BPoint(fTitleBarRect.right, fTitleBarRect.bottom), fSeparatorColor);
}


/*!	The surface of a shown tab: the active one is raised, with rounded top
	corners, and continues into the window contents; the others sit flat on
	the strip, the one under the pointer with a faint surface.
*/
void
ModernTabDecorator::_DrawTabSurface(StripTab* tab, BRect rect, bool active,
	bool focused, bool hovered)
{
	rgb_color strip = focused ? fStripColor : fInactiveStripColor;

	if (active) {
		fDrawingEngine->SetHighColor(fSurfaceColor);
		fDrawingEngine->DrawRoundRect(rect, kTabRadius, kTabRadius, true);
		fDrawingEngine->FillRect(BRect(rect.left, rect.top + kTabRadius,
			rect.right, rect.bottom), fSurfaceColor);
		if (fBorderWidth > 0) {
			fDrawingEngine->FillRect(BRect(rect.left, fTopBorder.top,
				rect.right, fTopBorder.bottom), fSurfaceColor);
		}

		if (focused) {
			// a short accent line in the system tab color marks the active
			// window of the focused stack
			BRect accent(rect.left + kTabRadius + 2, rect.top + 1,
				rect.right - kTabRadius - 2, rect.top + kAccentHeight);
			if (accent.IsValid())
				fDrawingEngine->FillRect(accent, fAccentColor);
		}
		return;
	}

	if (hovered) {
		fDrawingEngine->SetHighColor(blend(strip, fTextColor, 16));
		fDrawingEngine->DrawRoundRect(BRect(rect.left, rect.top,
			rect.right, rect.bottom - 1), kTabRadius, kTabRadius, true);
		fDrawingEngine->FillRect(BRect(rect.left, rect.top + kTabRadius,
			rect.right, rect.bottom - 1), blend(strip, fTextColor, 16));
	}
	fDrawingEngine->StrokeLine(BPoint(rect.left, fTitleBarRect.bottom),
		BPoint(rect.right, fTitleBarRect.bottom), fSeparatorColor);
}


void
ModernTabDecorator::_DrawStub(StripTab* tab)
{
	BRect rect = tab->tabRect;
	rect.InsetBy(1, 3);
	if (!rect.IsValid())
		return;

	bool focused = _IsFocusedStack();
	rgb_color strip = focused ? fStripColor : fInactiveStripColor;
	bool hovered = fHoverRegion == REGION_TAB
		&& fHoverTab == fTabList.IndexOf(tab);
	fDrawingEngine->SetHighColor(blend(strip, fTextColor,
		hovered ? 48 : 28));
	fDrawingEngine->DrawRoundRect(rect, 4, 4, true);

	// a chevron pointing to where the hidden tabs are
	bool left = tab->tabRect.left
		< (fTitleBarRect.left + fTitleBarRect.right) / 2;
	float cx = floorf((rect.left + rect.right) / 2) + 0.5f;
	float cy = floorf((rect.top + rect.bottom) / 2) + 0.5f;
	float dx = left ? -2.0f : 2.0f;
	rgb_color color = fInactiveTextColor;
	fDrawingEngine->StrokeLine(BPoint(cx - dx, cy - 4), BPoint(cx + dx, cy),
		color);
	fDrawingEngine->StrokeLine(BPoint(cx + dx, cy), BPoint(cx - dx, cy + 4),
		color);
}


void
ModernTabDecorator::_DrawTabCloseButton(StripTab* tab)
{
	BRect rect = tab->tabCloseRect;
	if (!_IsCloseShown(tab))
		return;

	int32 index = fTabList.IndexOf(tab);
	bool focused = _IsFocusedStack();
	bool active = _IsActive(tab);
	rgb_color background = active ? fSurfaceColor
		: (focused ? fStripColor : fInactiveStripColor);
	if (!active && _IsHovered(index))
		background = blend(background, fTextColor, 16);

	if (!tab->closeAlwaysShown) {
		// shown on hover only: cover the end of the title
		BRect cover(rect.left - kTitleCloseGap, tab->tabRect.top + 2,
			tab->tabRect.right - 2, tab->tabRect.bottom - 2);
		fDrawingEngine->FillRect(cover, background);
	}

	bool hovered = fHoverRegion == REGION_CLOSE_BUTTON && fHoverInTabClose
		&& fHoverTab == index;
	if (tab->closePressed && fCloseHitInTab) {
		fDrawingEngine->SetHighColor(blend(background, fTextColor, 70));
		fDrawingEngine->DrawRoundRect(rect, 4, 4, true);
	} else if (hovered) {
		fDrawingEngine->SetHighColor(blend(background, fTextColor, 40));
		fDrawingEngine->DrawRoundRect(rect, 4, 4, true);
	}

	rgb_color glyph = active && focused ? fTextColor : fInactiveTextColor;
	float cx = floorf((rect.left + rect.right) / 2) + 0.5f;
	float cy = floorf((rect.top + rect.bottom) / 2) + 0.5f;
	const float s = 3.5f;
	fDrawingEngine->StrokeLine(BPoint(cx - s, cy - s), BPoint(cx + s, cy + s),
		glyph);
	fDrawingEngine->StrokeLine(BPoint(cx + s, cy - s), BPoint(cx - s, cy + s),
		glyph);
}


void
ModernTabDecorator::_DrawControl(Decorator::Tab* tab, BRect rect,
	rgb_color color, int32 glyph, bool pressed, bool enabled, bool hovered)
{
	if (!rect.IsValid())
		return;

	bool focused = _IsFocusedStack();
	rgb_color strip = focused ? fStripColor : fInactiveStripColor;

	// background behind the round control
	fDrawingEngine->FillRect(rect, strip);

	// Controls of a stack without focus are neutral, so only the focused
	// stack's controls draw attention, until the pointer is over one.
	bool colored = enabled && (focused || hovered);
	if (!colored) {
		rgb_color neutral = blend(strip, fTextColor, enabled ? 60 : 30);
		fDrawingEngine->SetHighColor(neutral);
		fDrawingEngine->DrawEllipse(rect, true);
		if (!enabled)
			return;
	} else {
		rgb_color fill = color;
		if (pressed)
			fill = tint_color(color, B_DARKEN_2_TINT);
		else if (hovered)
			fill = tint_color(color, B_LIGHTEN_1_TINT);
		fDrawingEngine->SetHighColor(fill);
		fDrawingEngine->DrawEllipse(rect, true);
		fDrawingEngine->SetHighColor(tint_color(fill, B_DARKEN_1_TINT));
		fDrawingEngine->DrawEllipse(rect, false);
	}

	// a small glyph says what the control does
	rgb_color ink = colored ? tint_color(color, B_DARKEN_4_TINT)
		: blend(strip, fTextColor, 120);
	float cx = floorf((rect.left + rect.right) / 2) + 0.5f;
	float cy = floorf((rect.top + rect.bottom) / 2) + 0.5f;
	const float s = 2.5f;
	switch (glyph) {
		case GLYPH_CLOSE:
			fDrawingEngine->StrokeLine(BPoint(cx - s, cy - s),
				BPoint(cx + s, cy + s), ink);
			fDrawingEngine->StrokeLine(BPoint(cx + s, cy - s),
				BPoint(cx - s, cy + s), ink);
			break;
		case GLYPH_MINIMIZE:
			fDrawingEngine->StrokeLine(BPoint(cx - s, cy), BPoint(cx + s, cy),
				ink);
			break;
		case GLYPH_ZOOM:
			fDrawingEngine->StrokeLine(BPoint(cx - s, cy), BPoint(cx + s, cy),
				ink);
			fDrawingEngine->StrokeLine(BPoint(cx, cy - s), BPoint(cx, cy + s),
				ink);
			break;
	}
}


void
ModernTabDecorator::_DrawSeparatorAfter(int32 index)
{
	// a short divider between two inactive, fully shown tabs; none while
	// dragging, when the tabs are on the move
	if (fDragTab >= 0)
		return;
	StripTab* tab = _StripTabAt(index);
	StripTab* next = _StripTabAt(index + 1);
	if (tab == NULL || next == NULL)
		return;
	if (_IsActive(tab) || _IsActive(next) || tab->isStub || next->isStub
		|| !tab->tabRect.IsValid() || !next->tabRect.IsValid()) {
		return;
	}

	float x = floorf((tab->tabRect.right + next->tabRect.left) / 2);
	float top = tab->tabRect.top + 6;
	float bottom = fTitleBarRect.bottom - 5;
	if (bottom <= top)
		return;
	fDrawingEngine->StrokeLine(BPoint(x, top), BPoint(x, bottom),
		fSeparatorColor);
}


bool
ModernTabDecorator::_IsFocusedStack() const
{
	// Normally only the top window of a stack has the focus, but with focus
	// following the mouse a background tab's window can briefly have it.
	for (int32 i = 0; i < fTabList.CountItems(); i++) {
		if (fTabList.ItemAt(i)->isFocused)
			return true;
	}
	return false;
}


// #pragma mark - DecorAddOn


extern "C" DecorAddOn*
instantiate_decor_addon(image_id id, const char* name)
{
	return new (std::nothrow) ModernTabDecorAddOn(id, name);
}
