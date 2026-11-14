/*
 * Copyright 2026, Koan contributors. All rights reserved.
 * Distributed under the terms of the MIT License.
 */


/*!	KoiPop control look: glossy capsule buttons and matching controls. */


#include "KoiPopControlLook.h"

#include <algorithm>
#include <math.h>
#include <new>

#include <GradientLinear.h>
#include <InterfaceDefs.h>
#include <MenuBar.h>
#include <Rect.h>
#include <View.h>

#include <KoiPopColorScheme.h>


namespace BPrivate {


using KoiPop::Color;
using KoiPop::Palette;
using KoiPop::PillStyle;


// All sizes are logical units; app_server scales them when it rasterizes.

// The frame of a button: room for the shadow and the focus ring around the
// body, and for the second outline of the default button.
static const float kButtonMargin = 2.0f;
static const float kDefaultButtonMargin = 5.0f;

static const float kMenuItemRadius = 4.0f;
static const float kTextControlRadius = 3.0f;
static const float kCheckBoxRadius = 4.0f;
static const float kTabAccentHeight = 2.0f;

static const int32 kPopUpIndicatorWidth = B_USE_ITEM_SPACING;


// #pragma mark - helpers


static inline rgb_color
rgb(Color color)
{
	return make_color(color.red, color.green, color.blue, 255);
}


static inline rgb_color
rgba(Color color)
{
	return make_color(color.red, color.green, color.blue, color.alpha);
}


static inline Color
koipop(const rgb_color& color)
{
	return koipop_color(color);
}


static inline KoiPop::Rect
koipop_rect(const BRect& rect)
{
	return KoiPop::MakeRect(rect.left, rect.top, rect.right, rect.bottom);
}


static inline BRect
be_rect(const KoiPop::Rect& rect)
{
	return BRect(rect.left, rect.top, rect.right, rect.bottom);
}


//!	A flat button that nothing draws attention to stays flat.
static inline bool
flat_at_rest(uint32 flags)
{
	return (flags & BControlLook::B_FLAT) != 0
		&& (flags & (BControlLook::B_ACTIVATED
			| BControlLook::B_PARTIALLY_ACTIVATED)) == 0
		&& ((flags & (BControlLook::B_HOVER | BControlLook::B_FOCUSED)) == 0
			|| (flags & BControlLook::B_DISABLED) != 0);
}


/*!	The KoiPop state of a push button. The toolkit reports a pressed button
	and a toggled-on button the same way (B_ACTIVATED); both are drawn
	pushed in, with the selection mark.
*/
static uint32
button_state(uint32 flags)
{
	uint32 state = 0;
	if ((flags & BControlLook::B_HOVER) != 0)
		state |= KoiPop::kStateHover;
	if ((flags & (BControlLook::B_ACTIVATED | BControlLook::B_CLICKED)) != 0)
		state |= KoiPop::kStatePressed;
	if ((flags & (BControlLook::B_ACTIVATED
			| BControlLook::B_PARTIALLY_ACTIVATED)) != 0) {
		state |= KoiPop::kStateSelected;
	}
	if ((flags & BControlLook::B_FOCUSED) != 0)
		state |= KoiPop::kStateFocused;
	if ((flags & BControlLook::B_DISABLED) != 0)
		state |= KoiPop::kStateDisabled;
	if ((flags & BControlLook::B_DEFAULT_BUTTON) != 0)
		state |= KoiPop::kStateDefault;
	return state;
}


//!	The KoiPop state of a check box or radio button: checked is not pressed.
static uint32
mark_control_state(uint32 flags)
{
	uint32 state = 0;
	if ((flags & BControlLook::B_HOVER) != 0)
		state |= KoiPop::kStateHover;
	if ((flags & BControlLook::B_CLICKED) != 0)
		state |= KoiPop::kStatePressed;
	if ((flags & BControlLook::B_FOCUSED) != 0)
		state |= KoiPop::kStateFocused;
	if ((flags & BControlLook::B_DISABLED) != 0)
		state |= KoiPop::kStateDisabled;
	return state;
}


static inline bool
has_all_borders(uint32 borders)
{
	return (borders & BControlLook::B_ALL_BORDERS)
		== BControlLook::B_ALL_BORDERS;
}


//!	Insets \a rect by \a inset on the sides that have a border.
static inline void
inset_borders(BRect& rect, float inset, uint32 borders)
{
	if ((borders & BControlLook::B_LEFT_BORDER) != 0)
		rect.left += inset;
	if ((borders & BControlLook::B_TOP_BORDER) != 0)
		rect.top += inset;
	if ((borders & BControlLook::B_RIGHT_BORDER) != 0)
		rect.right -= inset;
	if ((borders & BControlLook::B_BOTTOM_BORDER) != 0)
		rect.bottom -= inset;
}


static inline void
fill_shape(BView* view, const BRect& rect, float radius,
	const BGradient& gradient)
{
	if (radius > 0)
		view->FillRoundRect(rect, radius, radius, gradient);
	else
		view->FillRect(rect, gradient);
}


static inline void
fill_shape(BView* view, const BRect& rect, float radius)
{
	if (radius > 0)
		view->FillRoundRect(rect, radius, radius);
	else
		view->FillRect(rect);
}


static KoiPop::Shape
button_shape(const BRect& body, uint32 flags, uint32 borders,
	bool roundedOnly)
{
	bool allBorders = has_all_borders(borders);
	if (flat_at_rest(flags))
		return KoiPop::kFlatShape;
	if (!allBorders)
		return KoiPop::kSquareShape;
	if (roundedOnly)
		return KoiPop::kRoundedShape;
	return KoiPop::ShapeFor(koipop_rect(body), true, false);
}


static PillStyle
button_style(const Palette& palette, const rgb_color& base, uint32 flags)
{
	KoiPop::PillKind kind = (flags & BControlLook::B_DEFAULT_BUTTON) != 0
		? KoiPop::kPrimaryPill : KoiPop::kNeutralPill;
	return KoiPop::PillStyleFor(palette, kind, button_state(flags),
		koipop(base), koipop(ui_color(B_CONTROL_TEXT_COLOR)),
		koipop(ui_color(B_PANEL_BACKGROUND_COLOR)));
}


// #pragma mark - KoiPopControlLook


KoiPopControlLook::KoiPopControlLook()
	:
	HaikuControlLook()
{
}


KoiPopControlLook::~KoiPopControlLook()
{
}


const KoiPop::Palette&
KoiPopControlLook::_Palette() const
{
	return KoiPop::GetPalette(KoiPop::AppearanceFor(
		koipop(ui_color(B_PANEL_BACKGROUND_COLOR))));
}


// #pragma mark - buttons


void
KoiPopControlLook::DrawButtonFrame(BView* view, BRect& rect,
	const BRect& updateRect, const rgb_color& base,
	const rgb_color& background, uint32 flags, uint32 borders)
{
	_DrawPillFrame(view, rect, updateRect, base, background, flags, borders,
		false);
}


void
KoiPopControlLook::DrawButtonFrame(BView* view, BRect& rect,
	const BRect& updateRect, float radius, const rgb_color& base,
	const rgb_color& background, uint32 flags, uint32 borders)
{
	// the theme decides the shape: capsules for push buttons
	_DrawPillFrame(view, rect, updateRect, base, background, flags, borders,
		false);
}


void
KoiPopControlLook::DrawButtonFrame(BView* view, BRect& rect,
	const BRect& updateRect, float leftTopRadius, float rightTopRadius,
	float leftBottomRadius, float rightBottomRadius, const rgb_color& base,
	const rgb_color& background, uint32 flags, uint32 borders)
{
	_DrawPillFrame(view, rect, updateRect, base, background, flags, borders,
		false);
}


void
KoiPopControlLook::DrawButtonBackground(BView* view, BRect& rect,
	const BRect& updateRect, const rgb_color& base, uint32 flags,
	uint32 borders, orientation orientation)
{
	_DrawPillBackground(view, rect, updateRect, base, flags, borders, false);
}


void
KoiPopControlLook::DrawButtonBackground(BView* view, BRect& rect,
	const BRect& updateRect, float radius, const rgb_color& base,
	uint32 flags, uint32 borders, orientation orientation)
{
	_DrawPillBackground(view, rect, updateRect, base, flags, borders, false);
}


void
KoiPopControlLook::DrawButtonBackground(BView* view, BRect& rect,
	const BRect& updateRect, float leftTopRadius, float rightTopRadius,
	float leftBottomRadius, float rightBottomRadius, const rgb_color& base,
	uint32 flags, uint32 borders, orientation orientation)
{
	_DrawPillBackground(view, rect, updateRect, base, flags, borders, false);
}


void
KoiPopControlLook::DrawButtonWithPopUpBackground(BView* view, BRect& rect,
	const BRect& updateRect, const rgb_color& base, uint32 flags,
	uint32 borders, orientation orientation)
{
	DrawButtonWithPopUpBackground(view, rect, updateRect, 0.0f, 0.0f, 0.0f,
		0.0f, base, flags, borders, orientation);
}


void
KoiPopControlLook::DrawButtonWithPopUpBackground(BView* view, BRect& rect,
	const BRect& updateRect, float radius, const rgb_color& base,
	uint32 flags, uint32 borders, orientation orientation)
{
	DrawButtonWithPopUpBackground(view, rect, updateRect, radius, radius,
		radius, radius, base, flags, borders, orientation);
}


void
KoiPopControlLook::DrawButtonWithPopUpBackground(BView* view, BRect& rect,
	const BRect& updateRect, float leftTopRadius, float rightTopRadius,
	float leftBottomRadius, float rightBottomRadius, const rgb_color& base,
	uint32 flags, uint32 borders, orientation orientation)
{
	_DrawPillBackground(view, rect, updateRect, base, flags, borders, false);
	if (!rect.IsValid())
		return;

	// a separated chevron at the end says the button opens a menu
	BRect indicator(rect);
	rect.right -= ComposeSpacing(kPopUpIndicatorWidth);
	indicator.left = rect.right + 3;
	if (!indicator.IsValid())
		return;

	PillStyle style = button_style(_Palette(), base, flags);
	view->PushState();
	view->SetHighColor(rgb(KoiPop::Mix(style.border, style.bottom, 0.4f)));
	view->StrokeLine(BPoint(rect.right + 1, rect.top + 3),
		BPoint(rect.right + 1, rect.bottom - 3));
	view->PopState();
	_DrawPopUpChevrons(view, indicator, rgb(style.text));
}


// #pragma mark - menu fields


void
KoiPopControlLook::DrawMenuFieldFrame(BView* view, BRect& rect,
	const BRect& updateRect, const rgb_color& base,
	const rgb_color& background, uint32 flags, uint32 borders)
{
	_DrawPillFrame(view, rect, updateRect, base, background,
		flags & ~B_DEFAULT_BUTTON, borders, true);
}


void
KoiPopControlLook::DrawMenuFieldFrame(BView* view, BRect& rect,
	const BRect& updateRect, float radius, const rgb_color& base,
	const rgb_color& background, uint32 flags, uint32 borders)
{
	_DrawPillFrame(view, rect, updateRect, base, background,
		flags & ~B_DEFAULT_BUTTON, borders, true);
}


void
KoiPopControlLook::DrawMenuFieldFrame(BView* view, BRect& rect,
	const BRect& updateRect, float leftTopRadius, float rightTopRadius,
	float leftBottomRadius, float rightBottomRadius, const rgb_color& base,
	const rgb_color& background, uint32 flags, uint32 borders)
{
	_DrawPillFrame(view, rect, updateRect, base, background,
		flags & ~B_DEFAULT_BUTTON, borders, true);
}


void
KoiPopControlLook::DrawMenuFieldBackground(BView* view, BRect& rect,
	const BRect& updateRect, const rgb_color& base, bool popupIndicator,
	uint32 flags)
{
	DrawMenuFieldBackground(view, rect, updateRect, 0.0f, 0.0f, 0.0f, 0.0f,
		base, popupIndicator, flags);
}


void
KoiPopControlLook::DrawMenuFieldBackground(BView* view, BRect& rect,
	const BRect& updateRect, float radius, const rgb_color& base,
	bool popupIndicator, uint32 flags)
{
	DrawMenuFieldBackground(view, rect, updateRect, radius, radius, radius,
		radius, base, popupIndicator, flags);
}


void
KoiPopControlLook::DrawMenuFieldBackground(BView* view, BRect& rect,
	const BRect& updateRect, float leftTopRadius, float rightTopRadius,
	float leftBottomRadius, float rightBottomRadius, const rgb_color& base,
	bool popupIndicator, uint32 flags)
{
	if (!rect.IsValid() || !ShouldDraw(view, rect, updateRect))
		return;

	flags &= ~B_DEFAULT_BUTTON;
	const Palette& palette = _Palette();
	BRect body(rect.InsetByCopy(-1, -1));
	float radius = std::max(0.0f,
		KoiPop::CornerRadius(KoiPop::kRoundedShape, koipop_rect(body)) - 1);

	PillStyle style = button_style(palette, base, flags);
	// a menu field is chosen from, not toggled: no selection mark
	style.showMark = false;
	_PaintSurface(view, rect, radius, style, B_HORIZONTAL);

	if (!popupIndicator)
		return;

	// The pop-up indicator is a primary colored cap at the end with up and
	// down chevrons: the field opens a list to choose from.
	BRect cap(rect);
	cap.left = cap.right - ComposeSpacing(kPopUpIndicatorWidth) + 1;
	if (cap.left <= rect.left)
		return;

	PillStyle capStyle = KoiPop::PillStyleFor(palette, KoiPop::kPrimaryPill,
		button_state(flags) & ~KoiPop::kStateSelected, koipop(base),
		koipop(ui_color(B_CONTROL_TEXT_COLOR)),
		koipop(ui_color(B_PANEL_BACKGROUND_COLOR)));

	view->PushState();
	view->ClipToRect(cap);
	_PaintSurface(view, rect, radius, capStyle, B_HORIZONTAL);
	view->PopState();

	view->PushState();
	view->SetHighColor(rgb(capStyle.border));
	view->StrokeLine(BPoint(cap.left, rect.top), BPoint(cap.left, rect.bottom));
	view->PopState();

	_DrawPopUpChevrons(view, cap, rgb(capStyle.text));
	rect.right = cap.left - 1;
}


void
KoiPopControlLook::DrawMenuFieldBackground(BView* view, BRect& rect,
	const BRect& updateRect, const rgb_color& base, uint32 flags,
	uint32 borders)
{
	if (!rect.IsValid() || !ShouldDraw(view, rect, updateRect))
		return;

	PillStyle style = button_style(_Palette(), base, flags & ~B_DEFAULT_BUTTON);
	style.showMark = false;
	_PaintSurface(view, rect, 0, style, B_HORIZONTAL);
}


// #pragma mark - menus


void
KoiPopControlLook::DrawMenuBarBackground(BView* view, BRect& rect,
	const BRect& updateRect, const rgb_color& base, uint32 flags,
	uint32 borders)
{
	if (!rect.IsValid() || !ShouldDraw(view, rect, updateRect))
		return;

	const Palette& palette = _Palette();

	// satin: lighter at the top, a step toward silver at the bottom
	Color top;
	Color bottom;
	KoiPop::MenuBarColors(palette, koipop(base), top, bottom);
	BGradientLinear gradient;
	gradient.AddColor(rgb(top), 0);
	gradient.AddColor(rgb(bottom), 255);
	gradient.SetStart(rect.LeftTop());
	gradient.SetEnd(rect.LeftBottom());
	view->FillRect(rect, gradient);

	if ((flags & B_ACTIVATED) != 0) {
		PillStyle style = KoiPop::MenuHighlightStyle(palette,
			koipop(ui_color(B_MENU_SELECTED_BACKGROUND_COLOR)),
			koipop(ui_color(B_MENU_SELECTED_ITEM_TEXT_COLOR)));
		BRect highlight(rect.InsetByCopy(1, 1));
		_PaintSurface(view, highlight, std::min(kMenuItemRadius,
			(highlight.Height() + 1) / 2), style, B_HORIZONTAL);
	}
}


void
KoiPopControlLook::DrawMenuItemBackground(BView* view, BRect& rect,
	const BRect& updateRect, const rgb_color& base, uint32 flags,
	uint32 borders)
{
	if (!rect.IsValid() || !ShouldDraw(view, rect, updateRect))
		return;

	if ((flags & B_ACTIVATED) == 0) {
		HaikuControlLook::DrawMenuItemBackground(view, rect, updateRect, base,
			flags, borders);
		return;
	}

	const Palette& palette = _Palette();
	rgb_color menuBackground = ui_color(B_MENU_BACKGROUND_COLOR);

	// the selected item is a glossy rounded bar on the menu (or menu bar)
	BMenuBar* menuBar = dynamic_cast<BMenuBar*>(view);
	BRect under(rect);
	if (menuBar != NULL)
		DrawMenuBarBackground(view, under, updateRect, menuBackground, 0, 0);
	else {
		view->PushState();
		view->SetHighColor(menuBackground);
		view->FillRect(rect);
		view->PopState();
	}

	BRect highlight(rect.InsetByCopy(menuBar != NULL ? 1 : 3, 1));
	if (!highlight.IsValid())
		highlight = rect;

	PillStyle style = KoiPop::MenuHighlightStyle(palette, koipop(base),
		koipop(ui_color(B_MENU_SELECTED_ITEM_TEXT_COLOR)));
	if ((flags & B_DISABLED) != 0) {
		style.top = KoiPop::Mix(style.top, koipop(menuBackground), 0.5f);
		style.bottom = KoiPop::Mix(style.bottom, koipop(menuBackground), 0.5f);
	}
	_PaintSurface(view, highlight, std::min(kMenuItemRadius,
		(highlight.Height() + 1) / 2), style, B_HORIZONTAL);

	view->SetLowColor(base);
}


// #pragma mark - progress, check boxes, radio buttons


void
KoiPopControlLook::DrawStatusBar(BView* view, BRect& rect,
	const BRect& updateRect, const rgb_color& base, const rgb_color& barColor,
	float progressPosition)
{
	if (!rect.IsValid() || !ShouldDraw(view, rect, updateRect))
		return;

	const Palette& palette = _Palette();
	const Color white = KoiPop::MakeColor(255, 255, 255);
	const Color black = KoiPop::MakeColor(0, 0, 0);

	view->PushState();

	// the corners outside the capsule
	view->SetHighColor(base);
	view->FillRect(rect);

	float radius = KoiPop::CornerRadius(KoiPop::kCapsuleShape,
		koipop_rect(rect));
	view->SetHighColor(rgb(palette.controlBorder));
	fill_shape(view, rect, radius);

	// a recessed track
	BRect inner(rect.InsetByCopy(1, 1));
	float innerRadius = std::max(0.0f, radius - 1);
	BGradientLinear track;
	track.AddColor(rgb(KoiPop::Mix(palette.trackBackground, black, 0.10f)), 0);
	track.AddColor(rgb(palette.trackBackground), 255);
	track.SetStart(inner.LeftTop());
	track.SetEnd(inner.LeftBottom());
	fill_shape(view, inner, innerRadius, track);

	// the glossy fill; an application's own bar color is kept
	BRect filled(inner);
	filled.right = std::min(inner.right, progressPosition - 1);
	if (filled.IsValid() && filled.Width() > 0) {
		PillStyle style = KoiPop::PillStyleFor(palette, KoiPop::kPrimaryPill,
			0, koipop(base), palette.onPrimary, koipop(base));
		if (barColor == ui_color(B_STATUS_BAR_COLOR)) {
			style.top = palette.progressTop;
			style.bottom = palette.progressBottom;
		} else {
			style.top = KoiPop::Mix(koipop(barColor), white, 0.25f);
			style.bottom = KoiPop::Mix(koipop(barColor), black, 0.10f);
		}
		style.glossTop = KoiPop::WithAlpha(white, 0.35f);
		style.showMark = false;

		view->PushState();
		view->ClipToRect(filled);
		_PaintSurface(view, inner, innerRadius, style, B_HORIZONTAL);
		view->PopState();

		// a crisp leading edge
		if (filled.right < inner.right) {
			view->SetHighColor(rgb(KoiPop::Mix(style.bottom, black, 0.25f)));
			view->StrokeLine(BPoint(filled.right, filled.top + 1),
				BPoint(filled.right, filled.bottom - 1));
		}
	}

	view->PopState();
	rect = inner;
}


void
KoiPopControlLook::DrawCheckBox(BView* view, BRect& rect,
	const BRect& updateRect, const rgb_color& base, uint32 flags)
{
	if (!rect.IsValid() || !ShouldDraw(view, rect, updateRect))
		return;

	const Palette& palette = _Palette();
	bool checked = (flags & (B_ACTIVATED | B_PARTIALLY_ACTIVATED)) != 0;
	PillStyle style = KoiPop::PillStyleFor(palette,
		checked ? KoiPop::kPrimaryPill : KoiPop::kNeutralPill,
		mark_control_state(flags), koipop(base),
		koipop(ui_color(B_CONTROL_TEXT_COLOR)),
		koipop(ui_color(B_PANEL_BACKGROUND_COLOR)));

	view->PushState();

	float radius = std::min(kCheckBoxRadius, KoiPop::CornerRadius(
		KoiPop::kRoundedShape, koipop_rect(rect)));
	// the outline; focus makes it a two unit ring in the focus color
	float borderWidth = style.showFocusRing ? 2.0f : 1.0f;
	view->SetHighColor(rgb(style.showFocusRing ? style.focusRing
		: style.border));
	fill_shape(view, rect, radius);

	BRect inner(rect.InsetByCopy(borderWidth, borderWidth));
	_PaintSurface(view, inner, std::max(0.0f, radius - borderWidth), style,
		B_HORIZONTAL);

	if (checked) {
		// the mark itself, in the label color of the filled box
		float size = inner.Width();
		float pen = std::max(1.5f, roundf(size / 6.0f * 2) / 2);
		view->SetHighColor(rgb(style.text));
		view->SetPenSize(pen);
		view->SetLineMode(B_ROUND_CAP, B_ROUND_JOIN);
		// the view flags are not part of the pushed state
		const uint32 viewFlags = view->Flags();
		view->SetFlags(viewFlags | B_SUBPIXEL_PRECISE);
		if ((flags & B_PARTIALLY_ACTIVATED) != 0) {
			float y = (inner.top + inner.bottom) / 2;
			view->StrokeLine(BPoint(inner.left + size * 0.25f, y),
				BPoint(inner.right - size * 0.25f, y));
		} else {
			BPoint points[3] = {
				BPoint(inner.left + size * 0.22f, inner.top + size * 0.52f),
				BPoint(inner.left + size * 0.42f, inner.top + size * 0.74f),
				BPoint(inner.left + size * 0.80f, inner.top + size * 0.28f)
			};
			view->StrokePolygon(points, 3, false);
		}
		view->SetFlags(viewFlags);
	}

	view->PopState();
	rect = inner;
}


void
KoiPopControlLook::DrawRadioButton(BView* view, BRect& rect,
	const BRect& updateRect, const rgb_color& base, uint32 flags)
{
	if (!rect.IsValid() || !ShouldDraw(view, rect, updateRect))
		return;

	const Palette& palette = _Palette();
	bool selected = (flags & B_ACTIVATED) != 0;
	PillStyle style = KoiPop::PillStyleFor(palette,
		selected ? KoiPop::kPrimaryPill : KoiPop::kNeutralPill,
		mark_control_state(flags), koipop(base),
		koipop(ui_color(B_CONTROL_TEXT_COLOR)),
		koipop(ui_color(B_PANEL_BACKGROUND_COLOR)));

	view->PushState();

	float borderWidth = style.showFocusRing ? 2.0f : 1.0f;
	view->SetHighColor(rgb(style.showFocusRing ? style.focusRing
		: style.border));
	view->FillEllipse(rect);

	// a glossy sphere
	BRect inner(rect.InsetByCopy(borderWidth, borderWidth));
	BGradientLinear fill;
	fill.AddColor(rgb(style.top), 0);
	fill.AddColor(rgb(style.bottom), 255);
	fill.SetStart(inner.LeftTop());
	fill.SetEnd(inner.LeftBottom());
	view->FillEllipse(inner, fill);

	BRect gloss(inner.left + inner.Width() * 0.2f, inner.top + 1,
		inner.right - inner.Width() * 0.2f,
		inner.top + inner.Height() * KoiPop::kGlossExtent);
	if (gloss.IsValid() && style.glossTop.alpha > 0) {
		view->SetDrawingMode(B_OP_ALPHA);
		view->SetBlendingMode(B_PIXEL_ALPHA, B_ALPHA_OVERLAY);
		BGradientLinear shine;
		shine.AddColor(rgba(style.glossTop), 0);
		shine.AddColor(rgba(style.glossBottom), 255);
		shine.SetStart(gloss.LeftTop());
		shine.SetEnd(gloss.LeftBottom());
		view->FillEllipse(gloss, shine);
		view->SetDrawingMode(B_OP_COPY);
	}

	if (selected) {
		// the dot, in the label color of the filled button
		float inset = roundf(inner.Width() * 0.32f);
		BRect dot(inner.InsetByCopy(inset, inset));
		if (dot.IsValid()) {
			view->SetHighColor(rgb(style.text));
			view->FillEllipse(dot);
		}
	}

	view->PopState();
	rect = inner;
}


// #pragma mark - scroll bars and sliders


void
KoiPopControlLook::DrawScrollBarBackground(BView* view, BRect& rect,
	const BRect& updateRect, const rgb_color& base, uint32 flags,
	orientation orientation)
{
	if (!rect.IsValid() || !ShouldDraw(view, rect, updateRect))
		return;

	const Palette& palette = _Palette();
	Color track = KoiPop::Mix(koipop(base), palette.trackBackground, 0.7f);
	if ((flags & B_DISABLED) != 0)
		track = KoiPop::Mix(track, koipop(base), 0.5f);

	// recessed: a shade along the leading edge
	BGradientLinear gradient;
	gradient.AddColor(rgb(KoiPop::Mix(track, KoiPop::MakeColor(0, 0, 0),
		0.07f)), 0);
	gradient.AddColor(rgb(track), 255);
	gradient.SetStart(rect.LeftTop());
	gradient.SetEnd(orientation == B_HORIZONTAL ? rect.LeftBottom()
		: rect.RightTop());

	view->PushState();
	view->ClipToRect(rect);
	view->FillRect(rect, gradient);
	view->PopState();
}


void
KoiPopControlLook::DrawScrollBarThumb(BView* view, BRect& rect,
	const BRect& updateRect, const rgb_color& base, uint32 flags,
	orientation orientation, uint32 knobStyle)
{
	if (!rect.IsValid() || !ShouldDraw(view, rect, updateRect))
		return;

	const Palette& palette = _Palette();

	// the thumb sits on its own piece of track
	BRect track(rect);
	DrawScrollBarBackground(view, track, updateRect, base, flags, orientation);

	BRect body(rect);
	if (orientation == B_HORIZONTAL)
		body.InsetBy(1, 2);
	else
		body.InsetBy(2, 1);
	if (!body.IsValid())
		return;

	PillStyle style = KoiPop::PillStyleFor(palette, KoiPop::kNeutralPill,
		mark_control_state(flags) & ~KoiPop::kStateFocused,
		koipop(ui_color(B_SCROLL_BAR_THUMB_COLOR)),
		koipop(ui_color(B_CONTROL_TEXT_COLOR)), koipop(base));
	float radius = KoiPop::CornerRadius(KoiPop::kCapsuleShape,
		koipop_rect(body));

	view->PushState();
	view->ClipToRect(rect);
	view->SetHighColor(rgb(style.border));
	fill_shape(view, body, radius);
	BRect inner(body.InsetByCopy(1, 1));
	if (inner.IsValid()) {
		_PaintSurface(view, inner, std::max(0.0f, radius - 1), style,
			orientation == B_HORIZONTAL ? B_HORIZONTAL : B_VERTICAL);
	}

	if (knobStyle != B_KNOB_NONE && inner.IsValid()) {
		// grip lines across the middle
		view->SetHighColor(rgb(KoiPop::Mix(style.border, style.bottom, 0.3f)));
		BPoint center((inner.left + inner.right) / 2,
			(inner.top + inner.bottom) / 2);
		for (int32 i = -1; i <= 1; i++) {
			if (orientation == B_HORIZONTAL) {
				float x = floorf(center.x) + i * 3;
				view->StrokeLine(BPoint(x, inner.top + 3),
					BPoint(x, inner.bottom - 3));
			} else {
				float y = floorf(center.y) + i * 3;
				view->StrokeLine(BPoint(inner.left + 3, y),
					BPoint(inner.right - 3, y));
			}
		}
	}
	view->PopState();
}


void
KoiPopControlLook::DrawSliderBar(BView* view, BRect rect,
	const BRect& updateRect, const rgb_color& base, rgb_color fillColor,
	uint32 flags, orientation orientation)
{
	if (!rect.IsValid() || !ShouldDraw(view, rect, updateRect))
		return;

	const Palette& palette = _Palette();
	const Color black = KoiPop::MakeColor(0, 0, 0);
	const Color white = KoiPop::MakeColor(255, 255, 255);

	view->PushState();
	view->ClipToRect(rect);

	if ((flags & B_BLEND_FRAME) == 0) {
		view->SetHighColor(base);
		view->FillRect(rect);
	}

	float radius = KoiPop::CornerRadius(KoiPop::kCapsuleShape,
		koipop_rect(rect));
	Color border = palette.controlBorder;
	Color fill = koipop(fillColor);
	if ((flags & B_DISABLED) != 0) {
		border = KoiPop::Mix(border, koipop(base), 0.5f);
		fill = KoiPop::Desaturate(KoiPop::Mix(fill, koipop(base), 0.5f),
			0.7f);
	}
	view->SetHighColor(rgb(border));
	fill_shape(view, rect, radius);

	// a recessed channel: darker where the light does not reach
	BRect inner(rect.InsetByCopy(1, 1));
	if (inner.IsValid()) {
		BGradientLinear gradient;
		gradient.AddColor(rgb(KoiPop::Mix(fill, black, 0.14f)), 0);
		gradient.AddColor(rgb(KoiPop::Mix(fill, white, 0.12f)), 255);
		gradient.SetStart(inner.LeftTop());
		gradient.SetEnd(orientation == B_HORIZONTAL ? inner.LeftBottom()
			: inner.RightTop());
		fill_shape(view, inner, std::max(0.0f, radius - 1), gradient);
	}

	view->PopState();
}


void
KoiPopControlLook::DrawSliderThumb(BView* view, BRect& rect,
	const BRect& updateRect, const rgb_color& base, uint32 flags,
	orientation orientation)
{
	if (!rect.IsValid() || !ShouldDraw(view, rect, updateRect))
		return;

	const Palette& palette = _Palette();
	PillStyle style = KoiPop::PillStyleFor(palette, KoiPop::kNeutralPill,
		mark_control_state(flags) & ~KoiPop::kStatePressed,
		koipop(ui_color(B_CONTROL_BACKGROUND_COLOR)),
		koipop(ui_color(B_CONTROL_TEXT_COLOR)), koipop(base));

	view->PushState();

	float radius = KoiPop::CornerRadius(KoiPop::kCapsuleShape,
		koipop_rect(rect));
	float borderWidth = style.showFocusRing ? 2.0f : 1.0f;
	view->SetHighColor(rgb(style.showFocusRing ? style.focusRing
		: style.border));
	fill_shape(view, rect, radius);

	BRect inner(rect.InsetByCopy(borderWidth, borderWidth));
	if (inner.IsValid()) {
		_PaintSurface(view, inner, std::max(0.0f, radius - borderWidth),
			style, B_HORIZONTAL);
	}

	view->PopState();
}


// #pragma mark - tabs, text fields, labels


void
KoiPopControlLook::DrawActiveTab(BView* view, BRect& rect,
	const BRect& updateRect, const rgb_color& base, uint32 flags,
	uint32 borders, uint32 side, int32 index, int32 selected, int32 first,
	int32 last)
{
	BRect tab(rect);
	HaikuControlLook::DrawActiveTab(view, rect, updateRect, base, flags,
		borders, side, index, selected, first, last);

	if (side != B_TOP_BORDER || (flags & B_DISABLED) != 0
		|| !tab.IsValid()) {
		return;
	}

	// the selected tab carries a primary colored accent along its top,
	// next to its raised, joined shape
	const Palette& palette = _Palette();
	BRect accent(floorf(tab.left) + 4, floorf(tab.top) + 1,
		floorf(tab.right) - 4, floorf(tab.top) + kTabAccentHeight);
	if (!accent.IsValid())
		return;

	view->PushState();
	view->ClipToRect(tab);
	view->SetHighColor(rgb(palette.ocean));
	view->FillRoundRect(accent, 1, 1);
	view->PopState();
}


void
KoiPopControlLook::DrawTextControlBorder(BView* view, BRect& rect,
	const BRect& updateRect, const rgb_color& base, uint32 flags,
	uint32 borders)
{
	if (!rect.IsValid())
		return;
	if (!ShouldDraw(view, rect, updateRect)) {
		inset_borders(rect, 2, borders);
		return;
	}

	const Palette& palette = _Palette();
	BRect outer(rect);
	BRect inner(rect);
	inset_borders(inner, 2, borders);

	bool disabled = (flags & B_DISABLED) != 0;
	Color border = palette.controlBorder;
	float borderWidth = 1;
	if (!disabled && (flags & B_INVALID) != 0) {
		border = koipop(ui_color(B_FAILURE_COLOR));
		borderWidth = 2;
	} else if (!disabled && (flags & B_FOCUSED) != 0) {
		border = palette.focusRing;
		borderWidth = 2;
	} else if (disabled)
		border = KoiPop::Mix(border, koipop(base), 0.5f);

	view->PushState();
	// only the frame band: the text view is inside
	view->ClipToRect(outer);
	view->ClipToInverseRect(inner);

	if ((flags & B_BLEND_FRAME) == 0) {
		view->SetHighColor(base);
		view->FillRect(outer);
	}

	float radius = has_all_borders(borders) ? kTextControlRadius : 0;
	view->SetHighColor(rgb(border));
	fill_shape(view, outer, radius);

	BRect field(outer);
	inset_borders(field, borderWidth, borders);
	rgb_color fieldColor = ui_color(B_DOCUMENT_BACKGROUND_COLOR);
	if (disabled)
		fieldColor = rgb(KoiPop::Mix(koipop(fieldColor), koipop(base), 0.5f));
	view->SetHighColor(fieldColor);
	fill_shape(view, field, std::max(0.0f, radius - borderWidth));

	// recessed: a soft shade under the top edge
	if (!disabled && (borders & B_TOP_BORDER) != 0) {
		view->SetDrawingMode(B_OP_ALPHA);
		view->SetBlendingMode(B_PIXEL_ALPHA, B_ALPHA_OVERLAY);
		view->SetHighColor(rgba(KoiPop::WithAlpha(KoiPop::MakeColor(0, 0, 0),
			0.10f)));
		view->StrokeLine(BPoint(field.left + radius, field.top),
			BPoint(field.right - radius, field.top));
	}

	view->PopState();
	rect = inner;
}


void
KoiPopControlLook::DrawLabel(BView* view, const char* label,
	const BBitmap* icon, BRect rect, const BRect& updateRect,
	const rgb_color& base, uint32 flags, const BAlignment& alignment,
	const rgb_color* textColor)
{
	if ((flags & B_DEFAULT_BUTTON) != 0 && !flat_at_rest(flags)) {
		// the default button is a saturated capsule: its label inverts;
		// the style already fades it when disabled
		PillStyle style = button_style(_Palette(), base, flags);
		rgb_color color = rgb(style.text);
		HaikuControlLook::DrawLabel(view, label, icon, rect, updateRect, base,
			flags & ~B_DISABLED, alignment, &color);
		return;
	}

	HaikuControlLook::DrawLabel(view, label, icon, rect, updateRect, base,
		flags, alignment, textColor);
}


// #pragma mark - private


/*!	The frame of a capsule control: background, soft shadow, focus ring,
	the default button's second outline, and the border. Leaves \a rect at
	the inside of the border for the background.
*/
void
KoiPopControlLook::_DrawPillFrame(BView* view, BRect& rect,
	const BRect& updateRect, const rgb_color& base,
	const rgb_color& background, uint32 flags, uint32 borders,
	bool roundedOnly)
{
	if (!rect.IsValid())
		return;

	const bool allBorders = has_all_borders(borders);
	const bool isDefault = (flags & B_DEFAULT_BUTTON) != 0 && allBorders;
	const float margin = isDefault ? kDefaultButtonMargin : kButtonMargin;

	BRect frame(rect);
	BRect body(rect);
	inset_borders(body, margin, borders);
	if (!body.IsValid()) {
		rect = body;
		return;
	}

	view->PushState();
	view->ClipToRect(frame);

	if ((flags & B_BLEND_FRAME) == 0) {
		view->SetHighColor(background);
		view->FillRect(frame);
	}

	KoiPop::Shape shape = button_shape(body, flags, borders, roundedOnly);
	if (shape == KoiPop::kFlatShape) {
		view->PopState();
		inset_borders(body, 1, borders);
		rect = body;
		return;
	}

	float radius = KoiPop::CornerRadius(shape, koipop_rect(body));
	PillStyle style = button_style(_Palette(), base, flags);

	if (style.shadowOffset > 0) {
		view->SetDrawingMode(B_OP_ALPHA);
		view->SetBlendingMode(B_PIXEL_ALPHA, B_ALPHA_OVERLAY);
		view->SetHighColor(rgba(style.shadow));
		fill_shape(view, body.OffsetByCopy(0, style.shadowOffset), radius);
		view->SetDrawingMode(B_OP_COPY);
	}

	if (allBorders && style.showDefaultRing) {
		// the default action has a second outline: a shape cue, not only
		// a color
		BRect ring(body.InsetByCopy(-2, -2));
		view->SetHighColor(rgb(style.defaultRing));
		view->SetPenSize(1);
		if (radius > 0)
			view->StrokeRoundRect(ring, radius + 2, radius + 2);
		else
			view->StrokeRect(ring);
	}

	Color border = style.border;
	if (style.showFocusRing) {
		if (allBorders) {
			// a two unit ring in the frame margin, around the body
			BRect ring(frame.InsetByCopy(1, 1));
			float ringRadius = radius > 0 ? radius + margin - 1 : 0;
			view->SetHighColor(rgb(style.focusRing));
			view->SetPenSize(2);
			if (ringRadius > 0)
				view->StrokeRoundRect(ring, ringRadius, ringRadius);
			else
				view->StrokeRect(ring);
			view->SetPenSize(1);
		} else {
			// a part of a larger control: its border shows the focus
			border = style.focusRing;
		}
	}

	// The border is the body filled in the border color; the background
	// fills the inside, so no seam shows at the rounded ends.
	view->SetHighColor(rgb(border));
	fill_shape(view, body, radius);

	view->PopState();

	inset_borders(body, 1, borders);
	rect = body;
}


//!	The convex surface inside the border drawn by _DrawPillFrame().
void
KoiPopControlLook::_DrawPillBackground(BView* view, BRect& rect,
	const BRect& updateRect, const rgb_color& base, uint32 flags,
	uint32 borders, bool roundedOnly)
{
	if (!rect.IsValid())
		return;

	BRect body(rect);
	inset_borders(body, -1, borders);
	KoiPop::Shape shape = button_shape(body, flags, borders, roundedOnly);

	if (shape == KoiPop::kFlatShape) {
		// at rest a flat button is the surface it sits on
		rgb_color flat = base;
		if (view->Parent() != NULL)
			flat = view->Parent()->LowColor();
		view->PushState();
		view->SetHighColor(flat);
		view->FillRect(rect);
		view->PopState();
		return;
	}

	float radius = std::max(0.0f,
		KoiPop::CornerRadius(shape, koipop_rect(body)) - 1);
	PillStyle style = button_style(_Palette(), base, flags);
	_PaintSurface(view, rect, radius, style, B_HORIZONTAL);
}


/*!	Paints a convex surface in \a body: the fill gradient, the gloss
	highlight, the inner shade of a pressed control and the selection mark.
	With \a orientation B_VERTICAL the light comes from the left (vertical
	scroll bar thumbs).
*/
void
KoiPopControlLook::_PaintSurface(BView* view, BRect body, float radius,
	const PillStyle& style, orientation orientation)
{
	if (!body.IsValid())
		return;

	const bool horizontal = orientation == B_HORIZONTAL;

	view->PushState();

	BGradientLinear fill;
	fill.AddColor(rgb(style.top), 0);
	fill.AddColor(rgb(style.bottom), 255);
	fill.SetStart(body.LeftTop());
	fill.SetEnd(horizontal ? body.LeftBottom() : body.RightTop());
	fill_shape(view, body, radius, fill);

	view->SetDrawingMode(B_OP_ALPHA);
	view->SetBlendingMode(B_PIXEL_ALPHA, B_ALPHA_OVERLAY);

	// the gloss: computed for a horizontal surface, turned for a vertical
	KoiPop::Rect surface = horizontal ? koipop_rect(body)
		: KoiPop::MakeRect(body.top, body.left, body.bottom, body.right);
	KoiPop::Rect glossRect;
	float glossRadius;
	KoiPop::GlossFor(surface, radius, glossRect, glossRadius);
	BRect gloss = horizontal ? be_rect(glossRect)
		: BRect(glossRect.top, glossRect.left, glossRect.bottom,
			glossRect.right);
	if (gloss.IsValid() && style.glossTop.alpha > 0) {
		BGradientLinear shine;
		shine.AddColor(rgba(style.glossTop), 0);
		shine.AddColor(rgba(style.glossBottom), 255);
		shine.SetStart(gloss.LeftTop());
		shine.SetEnd(horizontal ? gloss.LeftBottom() : gloss.RightTop());
		fill_shape(view, gloss, glossRadius, shine);
	}

	if (style.inset) {
		// pushed in: a shade under the upper edge
		Color black = KoiPop::MakeColor(0, 0, 0);
		BGradientLinear shade;
		shade.AddColor(rgba(KoiPop::WithAlpha(black, 0.24f)), 0);
		shade.AddColor(rgba(KoiPop::WithAlpha(black, 0.0f)), 96);
		shade.SetStart(body.LeftTop());
		shade.SetEnd(horizontal ? body.LeftBottom() : body.RightTop());
		fill_shape(view, body, radius, shade);
	}

	view->SetDrawingMode(B_OP_COPY);

	if (style.showMark) {
		// the selected state's own mark: a short bar near the lower edge
		view->SetHighColor(rgb(style.mark));
		if (horizontal) {
			float length = std::max(6.0f, floorf(body.Width() * 0.28f));
			float center = floorf((body.left + body.right) / 2);
			BRect mark(center - floorf(length / 2), body.bottom - 3,
				center + floorf(length / 2), body.bottom - 2);
			if (mark.top > body.top + 2)
				view->FillRoundRect(mark, 1, 1);
		} else {
			float length = std::max(6.0f, floorf(body.Height() * 0.28f));
			float center = floorf((body.top + body.bottom) / 2);
			BRect mark(body.right - 3, center - floorf(length / 2),
				body.right - 2, center + floorf(length / 2));
			if (mark.left > body.left + 2)
				view->FillRoundRect(mark, 1, 1);
		}
	}

	view->PopState();
}


//!	Up and down chevrons centered in \a rect: "choose from a list".
void
KoiPopControlLook::_DrawPopUpChevrons(BView* view, BRect rect,
	const rgb_color& color)
{
	if (!rect.IsValid())
		return;

	float cx = floorf((rect.left + rect.right) / 2) + 0.5f;
	float cy = floorf((rect.top + rect.bottom) / 2) + 0.5f;
	float half = std::max(2.0f, floorf(std::min(rect.Width(),
		rect.Height()) / 6));
	float gap = std::max(1.5f, half * 0.5f);

	// the view flags are not part of the pushed state
	const uint32 viewFlags = view->Flags();
	view->PushState();
	view->SetFlags(viewFlags | B_SUBPIXEL_PRECISE);
	view->SetHighColor(color);
	view->SetPenSize(1.5f);
	view->SetLineMode(B_ROUND_CAP, B_ROUND_JOIN);

	BPoint up[3] = {
		BPoint(cx - half, cy - gap),
		BPoint(cx, cy - gap - half),
		BPoint(cx + half, cy - gap)
	};
	BPoint down[3] = {
		BPoint(cx - half, cy + gap),
		BPoint(cx, cy + gap + half),
		BPoint(cx + half, cy + gap)
	};
	view->StrokePolygon(up, 3, false);
	view->StrokePolygon(down, 3, false);

	view->PopState();
	view->SetFlags(viewFlags);
}


}	// namespace BPrivate


extern "C" BControlLook* (instantiate_control_look)(image_id id)
{
	return new (std::nothrow) BPrivate::KoiPopControlLook();
}
