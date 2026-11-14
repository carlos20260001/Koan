/*
 * Copyright 2026, Koan contributors. All rights reserved.
 * Distributed under the terms of the MIT License.
 */


/*!	KoiPop window decorator: the tab strip decorator, glossy. */


#include "KoiPopDecorator.h"

#include <algorithm>
#include <math.h>
#include <new>

#include <GradientLinear.h>
#include <Point.h>
#include <Rect.h>

#include "DesktopSettings.h"
#include "DrawingEngine.h"


using KoiPop::Color;
using KoiPop::Palette;


// All sizes are logical units; app_server scales them when it rasterizes.
static const float kTabRadius = 6.0f;
static const float kAccentHeight = 2.0f;
static const float kPinstripePeriod = 2.0f;
	// one faint line every other row of the title area
static const float kGlyphSize = 2.5f;


static inline rgb_color
rgb(Color color)
{
	return make_color(color.red, color.green, color.blue, 255);
}


static inline Color
koipop(const rgb_color& color)
{
	return KoiPop::MakeColor(color.red, color.green, color.blue);
}


// #pragma mark - KoiPopDecorAddOn


KoiPopDecorAddOn::KoiPopDecorAddOn(image_id id, const char* name)
	:
	DecorAddOn(id, name)
{
}


Decorator*
KoiPopDecorAddOn::_AllocateDecorator(DesktopSettings& settings, BRect rect,
	Desktop* desktop)
{
	return new (std::nothrow) KoiPopDecorator(settings, rect, desktop);
}


// #pragma mark - KoiPopDecorator


KoiPopDecorator::KoiPopDecorator(DesktopSettings& settings, BRect frame,
	Desktop* desktop)
	:
	ModernTabDecorator(settings, frame, desktop),
	fPalette(&KoiPop::GetPalette(KoiPop::kLightAppearance))
{
	// the base class constructor ran its own palette
	_UpdatePalette(settings);
}


KoiPopDecorator::~KoiPopDecorator()
{
}


void
KoiPopDecorator::_UpdatePalette(DesktopSettings& settings)
{
	ModernTabDecorator::_UpdatePalette(settings);

	rgb_color panel = settings.UIColor(B_PANEL_BACKGROUND_COLOR);
	fPalette = &KoiPop::GetPalette(KoiPop::AppearanceFor(koipop(panel)));
	const Palette& p = *fPalette;

	// the active tab joins the window contents: panel colors
	fSurfaceColor = panel;
	fTextColor = settings.UIColor(B_PANEL_TEXT_COLOR);

	// the satin title area; the flat strip colors are its middle, used by
	// the frame borders, stubs and hover surfaces of the base class
	fStripColor = rgb(KoiPop::TitleColorAt(p, true, 0.5f));
	fInactiveStripColor = rgb(KoiPop::TitleColorAt(p, false, 0.5f));
	fInactiveTextColor = rgb(p.inactiveTitleText);
	fSeparatorColor = rgb(KoiPop::Mix(p.titleBottom, p.windowBorder, 0.55f));
	fBorderColor = rgb(p.windowBorder);
	fInactiveBorderColor = rgb(p.inactiveWindowBorder);
	fAccentColor = rgb(p.ocean);
	fCloseColor = rgb(p.closeControl);
	fMinimizeColor = rgb(p.minimizeControl);
	fZoomColor = rgb(p.zoomControl);
}


/*!	Satin metal: a vertical gradient with a faint pinstripe every other row,
	a pearl highlight along the top, the rounded top corners.
*/
void
KoiPopDecorator::_DrawStripBackground(BRect invalid)
{
	BRect area = fTitleBarRect & invalid;
	if (!fTitleBarRect.IsValid() || !area.IsValid())
		return;

	const Palette& p = *fPalette;
	const bool focused = _IsFocusedStack();
	const Color top = focused ? p.titleTop : p.inactiveTitleTop;
	const Color bottom = focused ? p.titleBottom : p.inactiveTitleBottom;

	BGradientLinear gradient;
	gradient.AddColor(rgb(top), 0);
	gradient.AddColor(rgb(bottom), 255);
	gradient.SetStart(fTitleBarRect.LeftTop());
	gradient.SetEnd(fTitleBarRect.LeftBottom());
	fDrawingEngine->FillRect(area, gradient);

	// pinstripes: opaque, precomputed per row, so no blending is needed
	const float height = std::max(1.0f, fTitleBarRect.Height());
	float first = fTitleBarRect.top + 1;
	if (area.top > first) {
		first += ceilf((area.top - first) / kPinstripePeriod)
			* kPinstripePeriod;
	}
	for (float y = first; y <= area.bottom && y < fTitleBarRect.bottom;
			y += kPinstripePeriod) {
		float fraction = (y - fTitleBarRect.top) / height;
		Color stripe = KoiPop::Over(KoiPop::TitleColorAt(p, focused,
			fraction), p.pinstripe);
		fDrawingEngine->StrokeLine(BPoint(area.left, y),
			BPoint(area.right, y), rgb(stripe));
	}

	// a pearl highlight under the top edge
	if (area.top <= fTitleBarRect.top + 1) {
		fDrawingEngine->StrokeLine(
			BPoint(area.left, fTitleBarRect.top + 1),
			BPoint(area.right, fTitleBarRect.top + 1),
			rgb(KoiPop::Mix(top, p.pearl, 0.6f)));
	}

	// a hairline separates the strip from the window contents; the active
	// tab covers it
	fDrawingEngine->StrokeLine(
		BPoint(fTitleBarRect.left, fTitleBarRect.bottom),
		BPoint(fTitleBarRect.right, fTitleBarRect.bottom), fSeparatorColor);

	// The rounded top corners: the cells outside the arc are cut out of the
	// window's shape (see the tab strip decorator); the rest of the corner
	// box takes the border color and the arc the satin gradient.
	const float radius = _CornerRadius();
	if (radius > 0) {
		BRect bounds = _OuterBounds();
		rgb_color border = focused ? fBorderColor : fInactiveBorderColor;
		for (int32 i = 0; i < 2; i++) {
			bool right = i == 1;
			BRect box(bounds.left, bounds.top, bounds.left + radius - 1,
				bounds.top + radius - 1);
			BRect circle(bounds.left, bounds.top,
				bounds.left + 2 * radius - 1, bounds.top + 2 * radius - 1);
			if (right) {
				box.OffsetBy(bounds.right - box.right, 0);
				circle.OffsetBy(bounds.right - circle.right, 0);
			}
			fDrawingEngine->FillRect(box, border);
			fDrawingEngine->DrawEllipse(circle, true, gradient);
		}
	}
}


void
KoiPopDecorator::_DrawTabSurface(StripTab* tab, BRect rect, bool active,
	bool focused, bool hovered)
{
	if (!active) {
		// flat on the satin strip; the hovered one gets a faint surface
		ModernTabDecorator::_DrawTabSurface(tab, rect, active, focused,
			hovered);
		return;
	}

	const Palette& p = *fPalette;
	const Color surface = koipop(fSurfaceColor);

	// a raised glossy surface with rounded top corners that continues into
	// the window contents: brighter at the top, settling into the window
	// color by the middle
	BGradientLinear gloss;
	gloss.AddColor(rgb(KoiPop::Mix(surface, p.pearl, 0.65f)), 0);
	gloss.AddColor(fSurfaceColor, 110);
	gloss.AddColor(fSurfaceColor, 255);
	gloss.SetStart(rect.LeftTop());
	gloss.SetEnd(rect.LeftBottom());
	fDrawingEngine->DrawRoundRect(rect, kTabRadius, kTabRadius, true, gloss);
	fDrawingEngine->FillRect(BRect(rect.left, rect.top + rect.Height() / 2,
		rect.right, rect.bottom), fSurfaceColor);
	if (fBorderWidth > 0) {
		fDrawingEngine->FillRect(BRect(rect.left, fTopBorder.top,
			rect.right, fTopBorder.bottom), fSurfaceColor);
	}

	// a clear outline along the top and sides, open toward the contents
	fDrawingEngine->SetHighColor(fSeparatorColor);
	fDrawingEngine->DrawRoundRect(rect, kTabRadius, kTabRadius, false);
	fDrawingEngine->FillRect(BRect(rect.left + 1, rect.bottom, rect.right - 1,
		rect.bottom), fSurfaceColor);

	if (focused) {
		// the ocean blue accent marks the active window of the focused
		// stack; its raised, joined shape marks it too
		BRect accent(rect.left + kTabRadius + 2, rect.top + 1,
			rect.right - kTabRadius - 2, rect.top + kAccentHeight);
		if (accent.IsValid())
			fDrawingEngine->FillRect(accent, fAccentColor);
	}
}


/*!	Titles: the active tab's in the panel text color on the window color;
	the other tabs' on the satin strip, in the title text color while the
	stack has the focus. Every pair is checked against WCAG AA by the KoiPop
	host test.
*/
void
KoiPopDecorator::_DrawTitle(Decorator::Tab* tab, BRect rect)
{
	if (tab->truncatedTitleLength <= 0 || !rect.IsValid())
		return;

	const Palette& p = *fPalette;
	const bool focused = _IsFocusedStack();
	const bool active = _IsActive(tab);

	rgb_color text;
	rgb_color background;
	if (active) {
		text = focused ? fTextColor : rgb(p.inactiveTitleText);
		background = fSurfaceColor;
	} else {
		text = focused ? rgb(p.titleText) : rgb(p.inactiveTitleText);
		background = focused ? fStripColor : fInactiveStripColor;
	}

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


/*!	A glossy jewel: a dark rim, a vertical gradient, a highlight over the
	upper half, and a glyph that is always shown, so the control is never
	told by its color alone. Controls of a stack without focus, and
	controls the window does not allow, are neutral; disabled ones have no
	glyph.
*/
void
KoiPopDecorator::_DrawControl(Decorator::Tab* tab, BRect rect,
	rgb_color color, int32 glyph, bool pressed, bool enabled, bool hovered)
{
	if (!rect.IsValid())
		return;

	const Palette& p = *fPalette;
	const bool focused = _IsFocusedStack();
	const Color white = KoiPop::MakeColor(255, 255, 255);
	const Color black = KoiPop::MakeColor(0, 0, 0);

	// the satin behind the round control
	_DrawStripBackground(rect);

	bool colored = enabled && (focused || hovered);
	Color fill = colored ? koipop(color) : p.disabledControl;
	if (!enabled)
		fill = KoiPop::Mix(fill, KoiPop::TitleColorAt(p, focused, 0.5f), 0.4f);
	if (pressed)
		fill = KoiPop::Mix(fill, black, 0.22f);
	else if (hovered && enabled)
		fill = KoiPop::Mix(fill, white, 0.15f);

	// rim
	fDrawingEngine->SetHighColor(rgb(KoiPop::WindowControlRim(fill)));
	fDrawingEngine->DrawEllipse(rect, true);

	// body
	BRect body(rect.InsetByCopy(1, 1));
	BGradientLinear gradient;
	gradient.AddColor(rgb(KoiPop::Mix(fill, black, 0.10f)), 0);
	gradient.AddColor(rgb(fill), 140);
	gradient.AddColor(rgb(KoiPop::Mix(fill, white, 0.28f)), 255);
	gradient.SetStart(body.LeftTop());
	gradient.SetEnd(body.LeftBottom());
	fDrawingEngine->DrawEllipse(body, true, gradient);

	// highlight: opaque, from almost white to the body color
	if (!pressed) {
		BRect shine(body.left + body.Width() * 0.22f, body.top + 0.5f,
			body.right - body.Width() * 0.22f,
			body.top + body.Height() * 0.48f);
		if (shine.IsValid()) {
			BGradientLinear highlight;
			highlight.AddColor(rgb(KoiPop::Mix(fill, white, 0.78f)), 0);
			highlight.AddColor(rgb(KoiPop::Mix(fill, white, 0.20f)), 255);
			highlight.SetStart(shine.LeftTop());
			highlight.SetEnd(shine.LeftBottom());
			fDrawingEngine->DrawEllipse(shine, true, highlight);
		}
	}

	if (!enabled)
		return;

	// the glyph says what the control does
	rgb_color ink = rgb(KoiPop::WindowControlInk(fill));
	float cx = floorf((rect.left + rect.right) / 2) + 0.5f;
	float cy = floorf((rect.top + rect.bottom) / 2) + 0.5f;
	const float s = kGlyphSize;
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


// #pragma mark - DecorAddOn


extern "C" DecorAddOn*
instantiate_decor_addon(image_id id, const char* name)
{
	return new (std::nothrow) KoiPopDecorAddOn(id, name);
}
