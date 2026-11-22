/*
 * Copyright 2026, Koan contributors
 * SPDX-License-Identifier: Apache-2.0
 */


/*! Modern Cuppertino-inspired window decorator with traffic light controls. */


#include "ModernDecorator.h"

#include <new>
#include <stdio.h>
#include <string.h>

#include <GradientLinear.h>
#include <Point.h>
#include <Rect.h>
#include <Region.h>
#include <View.h>

#include "DesktopSettings.h"
#include "DrawingEngine.h"
#include "RGBColor.h"


//#define DEBUG_MODERN_DECORATOR
#ifdef DEBUG_MODERN_DECORATOR
#	define STRACE(x) printf x
#else
#	define STRACE(x) ;
#endif


// Traffic light button size (points)
static const float kTrafficLightSize = 12.0f;
// Spacing between traffic light buttons
static const float kTrafficLightSpacing = 4.0f;
// Margin from the left edge of the title bar to the first button
static const float kTrafficLightMargin = 8.0f;
// Extra spacing after the last traffic light before the tab/title starts
static const float kTrafficLightTrailingSpacing = 6.0f;
// Title bar height
static const float kTitleBarHeight = 22.0f;
// Window border width
static const int32 kBorderWidth = 1;


ModernDecorAddOn::ModernDecorAddOn(image_id id, const char* name)
	:
	DecorAddOn(id, name)
{
}


Decorator*
ModernDecorAddOn::_AllocateDecorator(DesktopSettings& settings, BRect rect,
	Desktop* desktop)
{
	return new (std::nothrow) ModernDecorator(settings, rect, desktop);
}


ModernDecorator::ModernDecorator(DesktopSettings& settings, BRect frame,
	Desktop* desktop)
	:
	SATDecorator(settings, frame, desktop),
	fCloseColor((rgb_color) { 255, 95, 86, 255 }),     // Cuppertino close red
	fMinimizeColor((rgb_color) { 255, 189, 46, 255 }), // Cuppertino minimize amber
	fZoomColor((rgb_color) { 39, 201, 63, 255 }),      // Cuppertino maximize green
	fFrameColor((rgb_color) { 235, 235, 235, 255 }),   // light gray title bar
	fFocusTextColor(settings.UIColor(B_WINDOW_TEXT_COLOR)),
	fNonFocusTextColor(settings.UIColor(B_WINDOW_INACTIVE_TEXT_COLOR))
{
	STRACE(("ModernDecorator()\n"));
	STRACE(("\tFrame (%.1f,%.1f,%.1f,%.1f)\n",
		frame.left, frame.top, frame.right, frame.bottom));
}


ModernDecorator::~ModernDecorator()
{
	STRACE(("~ModernDecorator()\n"));
}


void
ModernDecorator::UpdateColors(DesktopSettings& settings)
{
	SATDecorator::UpdateColors(settings);

	fFocusTextColor = settings.UIColor(B_WINDOW_TEXT_COLOR);
	fNonFocusTextColor = settings.UIColor(B_WINDOW_INACTIVE_TEXT_COLOR);
}


void
ModernDecorator::Draw(BRect updateRect)
{
	STRACE(("ModernDecorator: Draw(BRect)\n"));

	fDrawingEngine->SetDrawState(&fDrawState);

	_DrawFrame(updateRect & fBorderRect);
	_DrawTabs(updateRect & fTitleBarRect);
}


void
ModernDecorator::Draw()
{
	STRACE("ModernDecorator::Draw()\n");
	fDrawingEngine->SetDrawState(&fDrawState);

	_DrawFrame(fBorderRect);
	_DrawTabs(fTitleBarRect);
}


Decorator::Region
ModernDecorator::RegionAt(BPoint where, int32& tab) const
{
	// Let the base class identify hits on buttons and tab first.
	Region region = SATDecorator::RegionAt(where, tab);
	if (region != REGION_NONE)
		return region;

	if (fTitleBarRect.Contains(where) && fTopTab != NULL) {
		for (int32 i = 0; i < fTabList.CountItems(); i++) {
			if (fTabList.ItemAt(i) == fTopTab) {
				tab = i;
				return REGION_TAB;
			}
		}
	}

	if (fTopTab == NULL || (fTopTab->flags & B_NOT_RESIZABLE) != 0
		|| !fBorderRect.Contains(where) || fFrame.Contains(where)) {
		return REGION_NONE;
	}

	if (where.x < fFrame.left)
		return REGION_LEFT_BORDER;
	if (where.x > fFrame.right)
		return REGION_RIGHT_BORDER;
	if (where.y > fFrame.bottom)
		return REGION_BOTTOM_BORDER;
	if (where.y < fFrame.top)
		return REGION_TOP_BORDER;

	return REGION_NONE;
}


bool
ModernDecorator::SetRegionHighlight(Region region, uint8 highlight,
	BRegion* dirty, int32 tabIndex)
{
	Decorator::Tab* tab
		= static_cast<Decorator::Tab*>(_TabAt(tabIndex));
	if (tab != NULL) {
		tab->isHighlighted = highlight != 0;
		// Invalidate the bitmap caches for the close/minimize/zoom button
		// when the highlight changes.
		switch (region) {
			case REGION_CLOSE_BUTTON:
				if (highlight != RegionHighlight(region))
					memset(&tab->closeBitmaps, 0, sizeof(tab->closeBitmaps));
				break;

			case REGION_MINIMIZE_BUTTON:
				if (highlight != RegionHighlight(region))
					memset(&tab->minimizeBitmaps, 0,
						sizeof(tab->minimizeBitmaps));
				break;

			case REGION_ZOOM_BUTTON:
				if (highlight != RegionHighlight(region))
					memset(&tab->zoomBitmaps, 0, sizeof(tab->zoomBitmaps));
				break;

			default:
				break;
		}
	}

	return SATDecorator::SetRegionHighlight(region, highlight, dirty, tabIndex);
}


void
ModernDecorator::_DoLayout()
{
	STRACE(("ModernDecorator: Do Layout\n"));

	bool hasTab = false;

	if (fTopTab) {
		switch (fTopTab->look) {
			case B_MODAL_WINDOW_LOOK:
				fBorderWidth = kBorderWidth;
				break;

			case B_TITLED_WINDOW_LOOK:
			case B_DOCUMENT_WINDOW_LOOK:
				hasTab = true;
				fBorderWidth = kBorderWidth;
				break;

			case B_FLOATING_WINDOW_LOOK:
				hasTab = true;
				fBorderWidth = kBorderWidth;
				break;

			case B_BORDERED_WINDOW_LOOK:
				fBorderWidth = kBorderWidth;
				break;

			default:
				fBorderWidth = 0;
		}
	} else {
		fBorderWidth = 0;
	}

	fBorderRect = fFrame.InsetByCopy(-fBorderWidth, -fBorderWidth);

	if (hasTab) {
		fTitleBarRect.Set(fFrame.left - fBorderWidth,
			fFrame.top - kTitleBarHeight,
			fFrame.right + fBorderWidth,
			fFrame.top - 1);

		const float buttonY = fTitleBarRect.top
			+ (kTitleBarHeight - kTrafficLightSize) / 2.0f;
		float buttonX = fTitleBarRect.left + kTrafficLightMargin;
		const float tabStart = buttonX + 3 * kTrafficLightSize
			+ 2 * kTrafficLightSpacing + kTrafficLightTrailingSpacing;
		float tabWidth = fTabList.CountItems() > 0
			? (fTitleBarRect.right - tabStart) / fTabList.CountItems() : 0;

		for (int32 i = 0; i < fTabList.CountItems(); i++) {
			Decorator::Tab* tab = fTabList.ItemAt(i);
			float tabLeft = tabStart + tabWidth * i;
			float tabRight = i == fTabList.CountItems() - 1
				? fTitleBarRect.right : tabStart + tabWidth * (i + 1);
			tab->tabRect.Set(tabLeft, fTitleBarRect.top, tabRight,
				fTitleBarRect.bottom);
			tab->textOffset = 4.0f;

			if (tab == fTopTab) {
				tab->closeRect.Set(buttonX, buttonY,
					buttonX + kTrafficLightSize, buttonY + kTrafficLightSize);
				buttonX += kTrafficLightSize + kTrafficLightSpacing;
				tab->minimizeRect.Set(buttonX, buttonY,
					buttonX + kTrafficLightSize, buttonY + kTrafficLightSize);
				buttonX += kTrafficLightSize + kTrafficLightSpacing;
				tab->zoomRect.Set(buttonX, buttonY,
					buttonX + kTrafficLightSize, buttonY + kTrafficLightSize);
				buttonX += kTrafficLightSize + kTrafficLightSpacing;
			} else {
				tab->closeRect.Set(0.0f, 0.0f, -1.0f, -1.0f);
				tab->minimizeRect.Set(0.0f, 0.0f, -1.0f, -1.0f);
				tab->zoomRect.Set(0.0f, 0.0f, -1.0f, -1.0f);
			}

			tab->truncatedTitle = Title(tab) != NULL ? Title(tab) : "";
			float availableWidth = tab->tabRect.Width() - 8.0f;
			if (availableWidth < 0.0f)
				availableWidth = 0.0f;
			fDrawState.Font().TruncateString(&tab->truncatedTitle,
				B_TRUNCATE_END, availableWidth);
			tab->truncatedTitleLength = tab->truncatedTitle.Length();
		}
	} else {
		fTitleBarRect.Set(0.0f, 0.0f, -1.0f, -1.0f);
		for (int32 i = 0; i < fTabList.CountItems(); i++) {
			Decorator::Tab* tab = fTabList.ItemAt(i);

			tab->tabRect.Set(0.0, 0.0, -1.0, -1.0);
			tab->closeRect.Set(0.0, 0.0, -1.0, -1.0);
			tab->minimizeRect.Set(0.0, 0.0, -1.0, -1.0);
			tab->zoomRect.Set(0.0, 0.0, -1.0, -1.0);
		}
	}
}


void
ModernDecorator::_DrawFrame(BRect invalid)
{
	if (fTopTab->look == B_NO_BORDER_WINDOW_LOOK)
		return;

	if (fBorderWidth <= 0)
		return;

	// Thin modern border around the window content area.
	rgb_color borderColor = IsFocus(fTopTab)
		? (rgb_color) { 170, 170, 170, 255 }
		: (rgb_color) { 205, 205, 205, 255 };

	fDrawingEngine->SetHighColor(borderColor);
	fDrawingEngine->StrokeRect(fBorderRect, borderColor);
}


void
ModernDecorator::_DrawTab(Decorator::Tab* tab, BRect invalid)
{
	if (!tab->tabRect.IsValid() || !invalid.Intersects(tab->tabRect))
		return;

	// Draw the tab / title bar background.
	rgb_color tabBg = IsFocus(tab)
		? fFrameColor
		: tint_color(fFrameColor, B_DARKEN_1_TINT);

	fDrawingEngine->SetHighColor(tabBg);
	fDrawingEngine->FillRect(tab->tabRect, tabBg);

	// Subtle separator line at the bottom of the title bar.
	BRect separatorLine(tab->tabRect.left, tab->tabRect.bottom - 1.0f,
		tab->tabRect.right, tab->tabRect.bottom);
	fDrawingEngine->SetHighColor(IsFocus(tab)
		? (rgb_color) { 200, 200, 200, 255 }
		: (rgb_color) { 180, 180, 180, 255 });
	fDrawingEngine->StrokeLine(separatorLine.LeftTop(),
		separatorLine.RightTop());

	_DrawTitle(tab, tab->tabRect);
	_DrawButtons(tab, invalid);
}


void
ModernDecorator::_DrawTitle(Decorator::Tab* tab, BRect rect)
{
	fDrawingEngine->SetHighColor(IsFocus(tab)
		? fFocusTextColor : fNonFocusTextColor);
	fDrawingEngine->SetLowColor(tint_color(fFrameColor, B_DARKEN_1_TINT));
	fDrawingEngine->SetFont(fDrawState.Font());

	font_height fontHeight;
	fDrawState.Font().GetHeight(fontHeight);

	BPoint titlePos;
	titlePos.x = rect.left + tab->textOffset;
	titlePos.y = floorf(((rect.top + 2.0f) + rect.bottom
		+ fontHeight.ascent + fontHeight.descent) / 2.0f
		- fontHeight.descent + 0.5f);

	fDrawingEngine->DrawString(tab->truncatedTitle.String(),
		tab->truncatedTitleLength, titlePos);
}


void
ModernDecorator::_DrawButtons(Decorator::Tab* tab, const BRect& invalid)
{
	if ((tab->flags & B_NOT_CLOSABLE) == 0
		&& invalid.Intersects(tab->closeRect)) {
		_DrawTrafficLight(tab, tab->closeRect, tab->closePressed,
			RegionHighlight(REGION_CLOSE_BUTTON) != 0, fCloseColor);
	}
	if ((tab->flags & B_NOT_MINIMIZABLE) == 0
		&& invalid.Intersects(tab->minimizeRect)) {
		_DrawTrafficLight(tab, tab->minimizeRect, tab->minimizePressed,
			RegionHighlight(REGION_MINIMIZE_BUTTON) != 0, fMinimizeColor);
	}
	if ((tab->flags & B_NOT_ZOOMABLE) == 0
		&& invalid.Intersects(tab->zoomRect)) {
		_DrawTrafficLight(tab, tab->zoomRect, tab->zoomPressed,
			RegionHighlight(REGION_ZOOM_BUTTON) != 0, fZoomColor);
	}
}


void
ModernDecorator::_DrawTrafficLight(Decorator::Tab* tab, BRect rect,
	bool pressed, bool highlighted, rgb_color color)
{
	// Dim the button color for inactive tabs.
	rgb_color buttonColor = color;
	if (!IsFocus(tab))
		buttonColor = tint_color(color, B_DARKEN_2_TINT);
	else if (highlighted)
		buttonColor = tint_color(color, B_LIGHTEN_1_TINT);

	// Fill the traffic light.
	fDrawingEngine->SetHighColor(buttonColor);
	fDrawingEngine->DrawRoundRect(rect, rect.Width() / 2.0f,
		rect.Height() / 2.0f, true);

	// Border.
	fDrawingEngine->SetHighColor(tint_color(buttonColor, B_DARKEN_2_TINT));
	fDrawingEngine->DrawRoundRect(rect, rect.Width() / 2.0f,
		rect.Height() / 2.0f, false);

	// Pressed overlay.
	if (pressed) {
		fDrawingEngine->SetHighColor((rgb_color) { 0, 0, 0, 70 });
		fDrawingEngine->DrawRoundRect(rect, rect.Width() / 2.0f,
			rect.Height() / 2.0f, true);
	}

	// Draw the glyph in the center.
	BPoint center = rect.LeftTop()
		+ BPoint(rect.Width() / 2.0f, rect.Height() / 2.0f);
	fDrawingEngine->SetHighColor((rgb_color) { 255, 255, 255, 230 });

	float s = 3.0f;
	if (color.red > 200 && color.green < 150 && color.blue < 150) {
		// Close: draw an X.
		fDrawingEngine->StrokeLine(BPoint(center.x - s, center.y - s),
			BPoint(center.x + s, center.y + s));
		fDrawingEngine->StrokeLine(BPoint(center.x + s, center.y - s),
			BPoint(center.x - s, center.y + s));
	} else if (color.green > 150 && color.red < 150) {
		// Zoom: draw a +.
		fDrawingEngine->StrokeLine(BPoint(center.x - s, center.y),
			BPoint(center.x + s, center.y));
		fDrawingEngine->StrokeLine(BPoint(center.x, center.y - s),
			BPoint(center.x, center.y + s));
	} else {
		// Minimize: draw a -.
		fDrawingEngine->StrokeLine(BPoint(center.x - s, center.y),
			BPoint(center.x + s, center.y));
	}
}


void
ModernDecorator::_DrawClose(Decorator::Tab* tab, bool direct, BRect rect)
{
	_DrawTrafficLight(tab, rect, tab->closePressed,
		RegionHighlight(REGION_CLOSE_BUTTON) != 0, fCloseColor);
}


void
ModernDecorator::_DrawMinimize(Decorator::Tab* tab, bool direct, BRect rect)
{
	_DrawTrafficLight(tab, rect, tab->minimizePressed,
		RegionHighlight(REGION_MINIMIZE_BUTTON) != 0, fMinimizeColor);
}


void
ModernDecorator::_DrawZoom(Decorator::Tab* tab, bool direct, BRect rect)
{
	_DrawTrafficLight(tab, rect, tab->zoomPressed,
		RegionHighlight(REGION_ZOOM_BUTTON) != 0, fZoomColor);
}


void
ModernDecorator::_SetTitle(Tab* tab, const char* string,
	BRegion* updateRegion)
{
	BRect rect = TabRect(tab);

	_DoLayout();

	if (updateRegion == NULL)
		return;

	rect = rect | TabRect(tab);
	rect.bottom++;
	updateRegion->Include(rect);
}


void
ModernDecorator::_MoveBy(BPoint offset)
{
	for (int32 i = 0; i < fTabList.CountItems(); i++) {
		Decorator::Tab* tab = fTabList.ItemAt(i);

		tab->zoomRect.OffsetBy(offset);
		tab->minimizeRect.OffsetBy(offset);
		tab->closeRect.OffsetBy(offset);
		tab->tabRect.OffsetBy(offset);
	}

	fFrame.OffsetBy(offset);
	fTitleBarRect.OffsetBy(offset);
	fBorderRect.OffsetBy(offset);
}


void
ModernDecorator::_ResizeBy(BPoint offset, BRegion* dirty)
{
	fFrame.right += offset.x;
	fFrame.bottom += offset.y;

	fTitleBarRect.right += offset.x;
	fBorderRect.right += offset.x;
	fBorderRect.bottom += offset.y;

	if (dirty) {
		dirty->Include(fTitleBarRect);
		dirty->Include(fBorderRect);
	}

	_DoLayout();
}


Decorator::Tab*
ModernDecorator::_AllocateNewTab()
{
	Decorator::Tab* tab = new(std::nothrow) Decorator::Tab;
	if (tab == NULL)
		return NULL;

	_SetFocus(tab);
	return tab;
}


bool
ModernDecorator::_AddTab(DesktopSettings& settings, int32 index,
	BRegion* updateRegion)
{
	_UpdateFont(settings);
	_DoLayout();
	if (updateRegion != NULL)
		updateRegion->Include(fTitleBarRect);
	return true;
}


bool
ModernDecorator::_RemoveTab(int32 index, BRegion* updateRegion)
{
	BRect oldTitle = fTitleBarRect;
	_DoLayout();
	if (updateRegion != NULL) {
		updateRegion->Include(oldTitle);
		updateRegion->Include(fTitleBarRect);
	}
	return true;
}


bool
ModernDecorator::_MoveTab(int32 from, int32 to, bool isMoving,
	BRegion* updateRegion)
{
	if (from < 0 || from >= fTabList.CountItems()
		|| to < 0 || to >= fTabList.CountItems())
		return false;

	Decorator::Tab* tab = fTabList.RemoveItemAt(from);
	if (tab == NULL)
		return false;

	if (fTabList.AddItem(tab, to) == false) {
		fTabList.AddItem(tab, from);
		return false;
	}

	_DoLayout();
	if (updateRegion != NULL)
		updateRegion->Include(fTitleBarRect);
	return true;
}


void
ModernDecorator::_GetFootprint(BRegion* region)
{
	if (!region)
		return;

	region->MakeEmpty();

	if (fTopTab->look == B_NO_BORDER_WINDOW_LOOK)
		return;

	region->Set(fBorderRect);
	region->Exclude(fFrame);

	if (fTopTab->look == B_BORDERED_WINDOW_LOOK)
		return;
	region->Include(fTitleBarRect);
}


void
ModernDecorator::_UpdateFont(DesktopSettings& settings)
{
	ServerFont font;
	settings.GetDefaultBoldFont(font);
	font.SetFlags(B_FORCE_ANTIALIASING);
	font.SetSpacing(B_STRING_SPACING);
	fDrawState.SetFont(font);
}


// #pragma mark - DecorAddOn

extern "C" DecorAddOn*
instantiate_decor_addon(image_id id, const char* name)
{
	return new (std::nothrow) ModernDecorAddOn(id, name);
}
