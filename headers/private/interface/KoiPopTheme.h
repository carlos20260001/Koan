/*
 * Copyright 2026, Koan contributors. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef _KOIPOP_THEME_H
#define _KOIPOP_THEME_H


/*!	Design tokens and pure geometry of the KoiPop theme.

	KoiPop is a glossy desktop theme: elongated capsule ("pill") buttons with
	a convex fill and an upper highlight, satin metal title areas with fine
	pinstripes, and a saturated ocean blue / turquoise palette with coral,
	orange, pink, lavender and green accents. Three parts share these
	tokens:

	- the KoiPopControlLook control-look add-on (Interface Kit controls),
	- the KoiPopDecorator window decorator add-on (app_server),
	- the KoiPop light and dark color schemes in the Appearance preferences
	  (KoiPopColorScheme.h).

	The header is plain C++11 without Be API types so a host test can check
	contrast ratios, state mapping and geometry without a window server.
	Colors are 8-bit sRGB. All geometry is in logical coordinates with BRect
	semantics: a rect covers the pixels left..right and top..bottom, both
	inclusive. app_server scales logical coordinates only when it
	rasterizes; nothing here depends on the render scale.
*/


#include <math.h>
#include <stdint.h>


namespace KoiPop {


// #pragma mark - colors


struct Color {
	uint8_t		red;
	uint8_t		green;
	uint8_t		blue;
	uint8_t		alpha;
};


inline Color
MakeColor(uint8_t red, uint8_t green, uint8_t blue, uint8_t alpha = 255)
{
	Color color = { red, green, blue, alpha };
	return color;
}


inline uint8_t
_ClampChannel(float value)
{
	if (value <= 0.0f)
		return 0;
	if (value >= 255.0f)
		return 255;
	return (uint8_t)(value + 0.5f);
}


/*!	Linear interpolation from \a from (\a amount 0) to \a to (\a amount 1).
	The result is opaque.
*/
inline Color
Mix(Color from, Color to, float amount)
{
	if (amount < 0.0f)
		amount = 0.0f;
	if (amount > 1.0f)
		amount = 1.0f;
	return MakeColor(
		_ClampChannel(from.red + (to.red - from.red) * amount),
		_ClampChannel(from.green + (to.green - from.green) * amount),
		_ClampChannel(from.blue + (to.blue - from.blue) * amount));
}


//!	\a overlay, with its alpha, composed over the opaque \a background.
inline Color
Over(Color background, Color overlay)
{
	return Mix(background, overlay, overlay.alpha / 255.0f);
}


inline Color
WithAlpha(Color color, float alpha)
{
	color.alpha = _ClampChannel(alpha * 255.0f);
	return color;
}


inline double
_LinearChannel(uint8_t channel)
{
	double value = channel / 255.0;
	if (value <= 0.04045)
		return value / 12.92;
	return pow((value + 0.055) / 1.055, 2.4);
}


//!	Relative luminance as defined by WCAG 2.x.
inline double
RelativeLuminance(Color color)
{
	return 0.2126 * _LinearChannel(color.red)
		+ 0.7152 * _LinearChannel(color.green)
		+ 0.0722 * _LinearChannel(color.blue);
}


//!	WCAG 2.x contrast ratio, 1 to 21. Alpha is ignored.
inline double
ContrastRatio(Color a, Color b)
{
	double la = RelativeLuminance(a);
	double lb = RelativeLuminance(b);
	if (la < lb) {
		double swap = la;
		la = lb;
		lb = swap;
	}
	return (la + 0.05) / (lb + 0.05);
}


//!	Moves \a color toward its own gray by \a amount (0 to 1).
inline Color
Desaturate(Color color, float amount)
{
	float gray = 0.299f * color.red + 0.587f * color.green
		+ 0.114f * color.blue;
	uint8_t level = _ClampChannel(gray);
	return Mix(color, MakeColor(level, level, level), amount);
}


inline bool
IsDarkColor(Color color)
{
	return RelativeLuminance(color) < 0.18;
}


// WCAG 2.x AA thresholds
static const double kMinTextContrast = 4.5;
static const double kMinLargeTextContrast = 3.0;
static const double kMinGraphicContrast = 3.0;


// #pragma mark - palette


enum Appearance {
	kLightAppearance = 0,
	kDarkAppearance = 1
};


/*!	The KoiPop tokens of one appearance. Fill tokens that carry text come
	with the text token that goes on them; the host test checks every such
	pair against WCAG AA.
*/
struct Palette {
	// brand
	Color		ocean;				// primary
	Color		oceanDeep;
	Color		turquoise;			// secondary
	Color		coral;
	Color		orange;
	Color		pink;
	Color		lavender;
	Color		green;

	// neutrals
	Color		pearl;
	Color		satin;
	Color		graphite;
	Color		midnight;

	// semantic fills, with the text that goes on them
	Color		info;
	Color		onInfo;
	Color		success;
	Color		onSuccess;
	Color		warning;
	Color		onWarning;
	Color		destructive;
	Color		onDestructive;

	// surfaces and text
	Color		windowBackground;
	Color		windowText;
	Color		secondaryText;
	Color		documentBackground;
	Color		documentText;
	Color		linkText;
	Color		linkVisitedText;

	// controls
	Color		controlBackground;	// base of a neutral pill
	Color		controlText;
	Color		controlBorder;
	Color		primaryTop;			// default/primary pill
	Color		primaryBottom;
	Color		primaryBorder;
	Color		onPrimary;
	Color		selectionMark;		// persistent mark of a selected control
	Color		focusRing;
	Color		trackBackground;	// slider, scroll bar and progress tracks
	Color		progressTop;
	Color		progressBottom;
	Color		shadow;				// soft drop shadow, with alpha

	// menus, lists, tool tips
	Color		menuBackground;
	Color		menuText;
	Color		menuSelectedBackground;
	Color		menuSelectedText;
	Color		listBackground;
	Color		listText;
	Color		listSelectedBackground;
	Color		listSelectedText;
	Color		toolTipBackground;
	Color		toolTipText;

	// window decoration
	Color		titleTop;			// satin metal title area
	Color		titleBottom;
	Color		titleText;
	Color		inactiveTitleTop;
	Color		inactiveTitleBottom;
	Color		inactiveTitleText;
	Color		pinstripe;			// with alpha, over the title gradient
	Color		tabSurface;			// the active tab, joins the window
	Color		windowBorder;
	Color		inactiveWindowBorder;
	Color		closeControl;
	Color		minimizeControl;
	Color		zoomControl;
	Color		disabledControl;

	// gloss: a white highlight over the upper part of convex surfaces
	float		glossTopAlpha;
	float		glossBottomAlpha;
};


inline const Palette&
GetPalette(Appearance appearance)
{
	static const Palette kLight = {
		// brand
		MakeColor(31, 102, 201),	// ocean
		MakeColor(14, 79, 168),		// oceanDeep
		MakeColor(25, 167, 184),	// turquoise
		MakeColor(242, 107, 91),	// coral
		MakeColor(245, 154, 58),	// orange
		MakeColor(242, 123, 166),	// pink
		MakeColor(169, 155, 230),	// lavender
		MakeColor(76, 194, 122),	// green

		// neutrals
		MakeColor(247, 249, 252),	// pearl
		MakeColor(213, 220, 229),	// satin
		MakeColor(42, 49, 59),		// graphite
		MakeColor(20, 33, 61),		// midnight

		// semantic
		MakeColor(31, 102, 201),	MakeColor(255, 255, 255),	// info
		MakeColor(30, 125, 69),		MakeColor(255, 255, 255),	// success
		MakeColor(242, 179, 61),	MakeColor(30, 42, 56),		// warning
		MakeColor(196, 55, 43),		MakeColor(255, 255, 255),	// destructive

		// surfaces and text
		MakeColor(233, 237, 242),	// windowBackground
		MakeColor(30, 42, 56),		// windowText
		MakeColor(79, 93, 112),		// secondaryText
		MakeColor(255, 255, 255),	// documentBackground
		MakeColor(30, 42, 56),		// documentText
		MakeColor(22, 90, 184),		// linkText
		MakeColor(104, 70, 176),	// linkVisitedText (deep lavender)

		// controls
		MakeColor(242, 245, 249),	// controlBackground
		MakeColor(30, 42, 56),		// controlText
		MakeColor(115, 129, 150),	// controlBorder
		MakeColor(32, 99, 198),		// primaryTop
		MakeColor(14, 70, 160),		// primaryBottom
		MakeColor(12, 62, 135),		// primaryBorder
		MakeColor(255, 255, 255),	// onPrimary
		MakeColor(14, 79, 168),		// selectionMark (deep ocean)
		MakeColor(31, 102, 201),	// focusRing
		MakeColor(208, 215, 225),	// trackBackground
		MakeColor(34, 150, 190),	// progressTop
		MakeColor(18, 88, 178),		// progressBottom
		MakeColor(20, 33, 61, 60),	// shadow

		// menus, lists, tool tips
		MakeColor(241, 244, 248),	// menuBackground
		MakeColor(30, 42, 56),		// menuText
		MakeColor(31, 102, 201),	// menuSelectedBackground
		MakeColor(255, 255, 255),	// menuSelectedText
		MakeColor(255, 255, 255),	// listBackground
		MakeColor(30, 42, 56),		// listText
		MakeColor(207, 224, 247),	// listSelectedBackground
		MakeColor(20, 33, 61),		// listSelectedText
		MakeColor(255, 251, 234),	// toolTipBackground
		MakeColor(30, 42, 56),		// toolTipText

		// window decoration
		MakeColor(244, 246, 249),	// titleTop
		MakeColor(201, 209, 220),	// titleBottom
		MakeColor(30, 42, 56),		// titleText
		MakeColor(240, 242, 245),	// inactiveTitleTop
		MakeColor(225, 229, 234),	// inactiveTitleBottom
		MakeColor(84, 96, 112),		// inactiveTitleText
		MakeColor(255, 255, 255, 70),	// pinstripe
		MakeColor(233, 237, 242),	// tabSurface
		MakeColor(111, 124, 142),	// windowBorder
		MakeColor(160, 170, 184),	// inactiveWindowBorder
		MakeColor(229, 83, 75),		// closeControl
		MakeColor(240, 165, 58),	// minimizeControl
		MakeColor(77, 184, 97),		// zoomControl
		MakeColor(178, 186, 197),	// disabledControl

		0.85f, 0.10f
	};

	static const Palette kDark = {
		// brand
		MakeColor(42, 111, 208),	// ocean
		MakeColor(24, 82, 165),		// oceanDeep
		MakeColor(38, 186, 204),	// turquoise
		MakeColor(244, 122, 107),	// coral
		MakeColor(247, 166, 79),	// orange
		MakeColor(244, 138, 178),	// pink
		MakeColor(184, 172, 238),	// lavender
		MakeColor(94, 205, 138),	// green

		// neutrals
		MakeColor(247, 249, 252),	// pearl
		MakeColor(213, 220, 229),	// satin
		MakeColor(42, 49, 59),		// graphite
		MakeColor(20, 33, 61),		// midnight

		// semantic
		MakeColor(31, 102, 201),	MakeColor(255, 255, 255),	// info
		MakeColor(30, 125, 69),		MakeColor(255, 255, 255),	// success
		MakeColor(242, 179, 61),	MakeColor(30, 42, 56),		// warning
		MakeColor(196, 55, 43),		MakeColor(255, 255, 255),	// destructive

		// surfaces and text
		MakeColor(27, 34, 48),		// windowBackground
		MakeColor(230, 236, 243),	// windowText
		MakeColor(163, 175, 191),	// secondaryText
		MakeColor(18, 23, 31),		// documentBackground
		MakeColor(230, 236, 243),	// documentText
		MakeColor(122, 178, 255),	// linkText
		MakeColor(190, 176, 244),	// linkVisitedText (lavender)

		// controls
		MakeColor(57, 66, 79),		// controlBackground
		MakeColor(242, 245, 249),	// controlText
		MakeColor(131, 145, 165),	// controlBorder
		MakeColor(32, 99, 198),		// primaryTop
		MakeColor(22, 78, 160),		// primaryBottom
		MakeColor(110, 165, 240),	// primaryBorder
		MakeColor(255, 255, 255),	// onPrimary
		MakeColor(64, 205, 222),	// selectionMark (bright turquoise)
		MakeColor(90, 162, 255),	// focusRing
		MakeColor(42, 50, 63),		// trackBackground
		MakeColor(46, 183, 200),	// progressTop
		MakeColor(42, 111, 208),	// progressBottom
		MakeColor(0, 0, 0, 110),	// shadow

		// menus, lists, tool tips
		MakeColor(35, 43, 56),		// menuBackground
		MakeColor(230, 236, 243),	// menuText
		MakeColor(35, 103, 198),	// menuSelectedBackground
		MakeColor(255, 255, 255),	// menuSelectedText
		MakeColor(18, 23, 31),		// listBackground
		MakeColor(230, 236, 243),	// listText
		MakeColor(46, 74, 115),		// listSelectedBackground
		MakeColor(242, 245, 249),	// listSelectedText
		MakeColor(46, 54, 68),		// toolTipBackground
		MakeColor(242, 245, 249),	// toolTipText

		// window decoration
		MakeColor(54, 62, 76),		// titleTop
		MakeColor(34, 40, 51),		// titleBottom
		MakeColor(230, 236, 243),	// titleText
		MakeColor(42, 48, 58),		// inactiveTitleTop
		MakeColor(33, 38, 46),		// inactiveTitleBottom
		MakeColor(166, 177, 192),	// inactiveTitleText
		MakeColor(255, 255, 255, 14),	// pinstripe
		MakeColor(27, 34, 48),		// tabSurface
		MakeColor(12, 16, 23),		// windowBorder
		MakeColor(20, 25, 33),		// inactiveWindowBorder
		MakeColor(236, 95, 86),		// closeControl
		MakeColor(242, 172, 66),	// minimizeControl
		MakeColor(86, 192, 106),	// zoomControl
		MakeColor(88, 97, 111),		// disabledControl

		0.22f, 0.03f
	};

	return appearance == kDarkAppearance ? kDark : kLight;
}


//!	The appearance whose window background is closest to \a background.
inline Appearance
AppearanceFor(Color background)
{
	return IsDarkColor(background) ? kDarkAppearance : kLightAppearance;
}


// #pragma mark - control states


enum {
	kStateHover		= 1 << 0,
	kStatePressed	= 1 << 1,
	kStateFocused	= 1 << 2,
	kStateDisabled	= 1 << 3,
	kStateSelected	= 1 << 4,
	kStateDefault	= 1 << 5
		// primary/default action
};


enum PillKind {
	kNeutralPill = 0,
	kPrimaryPill,
	kDestructivePill
};


// Part of a pill's height, from the top, the gloss highlight covers.
static const float kGlossExtent = 0.48f;
// Vertical span of a centered label, as parts of the pill's height.
static const float kLabelTop = 0.25f;
static const float kLabelBottom = 0.75f;


/*!	How a capsule control looks in one state. Every color is opaque except
	glossTop/glossBottom and shadow, which are composed over what is below.
*/
struct PillStyle {
	Color		top;				// fill gradient, top and bottom
	Color		bottom;
	Color		border;
	Color		text;
	Color		glossTop;			// white with alpha, see kGlossExtent
	Color		glossBottom;
	Color		shadow;				// drop shadow below the pill
	Color		focusRing;
	Color		mark;				// selection mark
	Color		defaultRing;		// outline of the default action
	float		shadowOffset;		// 0 when no drop shadow is drawn
	bool		inset;				// pressed: inner shadow, no drop shadow
	bool		showFocusRing;
	bool		showMark;
	bool		showDefaultRing;
		// the default action is marked by shape too: a second outline
};


/*!	The look of a capsule control of \a kind in \a state (kState* flags),
	for a neutral fill based on \a base (the toolkit's control background)
	and \a text, on a window background \a background.
*/
inline PillStyle
PillStyleFor(const Palette& palette, PillKind kind, uint32_t state,
	Color base, Color text, Color background)
{
	const bool dark = IsDarkColor(base);
	const bool darkAppearance = IsDarkColor(background);
	const bool colored = kind != kNeutralPill;
	const Color white = MakeColor(255, 255, 255);
	const Color black = MakeColor(0, 0, 0);

	PillStyle style;
	float glossTop = palette.glossTopAlpha;
	float glossBottom = palette.glossBottomAlpha;

	if (kind == kPrimaryPill) {
		style.top = palette.primaryTop;
		style.bottom = palette.primaryBottom;
		style.border = palette.primaryBorder;
		style.text = palette.onPrimary;
		// a weaker highlight keeps the white label readable
		glossTop = 0.18f;
		glossBottom = 0.03f;
	} else if (kind == kDestructivePill) {
		style.top = palette.destructive;
		style.bottom = Mix(palette.destructive, black, 0.22f);
		// the outline steps away from the window background
		style.border = darkAppearance
			? Mix(palette.destructive, white, 0.35f)
			: Mix(palette.destructive, black, 0.35f);
		style.text = palette.onDestructive;
		glossTop = 0.16f;
		glossBottom = 0.03f;
	} else {
		// convex: lighter at the top, a step darker at the bottom
		style.top = Mix(base, white, dark ? 0.10f : 0.70f);
		style.bottom = Mix(base, dark ? black : palette.satin,
			dark ? 0.22f : 0.55f);
		style.border = palette.controlBorder;
		style.text = text;
	}

	style.shadow = palette.shadow;
	style.focusRing = palette.focusRing;
	style.mark = colored ? style.text : palette.selectionMark;
	style.shadowOffset = 1.0f;
	style.inset = false;
	style.showFocusRing = (state & kStateFocused) != 0;
	style.showMark = false;
	style.defaultRing = palette.primaryBorder;
	style.showDefaultRing = (state & kStateDefault) != 0;

	if ((state & kStateDisabled) != 0) {
		style.defaultRing = Desaturate(Mix(style.defaultRing, background,
			0.5f), 0.7f);
		// desaturated and receding; no gloss, no shadow
		style.top = Desaturate(Mix(style.top, background, 0.45f), 0.7f);
		style.bottom = Desaturate(Mix(style.bottom, background, 0.45f), 0.7f);
		style.border = Desaturate(Mix(style.border, background, 0.5f), 0.7f);
		style.text = Mix(style.text, Mix(style.top, style.bottom, 0.5f),
			0.25f);
		glossTop *= 0.4f;
		glossBottom = 0.0f;
		style.shadowOffset = 0.0f;
		style.showFocusRing = false;
	} else if ((state & kStatePressed) != 0) {
		// pushed in: darker, the gradient turned over, little highlight
		Color top = style.top;
		style.top = Mix(style.bottom, black, dark ? 0.18f : 0.10f);
		style.bottom = Mix(top, black, dark ? 0.05f : 0.03f);
		glossTop *= 0.3f;
		glossBottom = 0.0f;
		style.shadowOffset = 0.0f;
		style.inset = true;
	} else if ((state & kStateHover) != 0) {
		// brighter, and lifted by a slightly longer shadow; colored fills
		// brighten less, their label is white
		float topAmount = colored ? 0.06f : (dark ? 0.06f : 0.35f);
		float bottomAmount = colored ? 0.04f : (dark ? 0.05f : 0.15f);
		style.top = Mix(style.top, white, topAmount);
		style.bottom = Mix(style.bottom, white, bottomAmount);
		style.shadowOffset = 2.0f;
	}

	if ((state & kStateSelected) != 0 && (state & kStateDisabled) == 0) {
		// a richer fill, plus a mark that does not rely on color alone
		if (kind == kNeutralPill) {
			style.top = Mix(style.top, palette.ocean, dark ? 0.22f : 0.16f);
			style.bottom = Mix(style.bottom, palette.ocean,
				dark ? 0.26f : 0.22f);
		}
		style.showMark = true;
	}

	if (glossTop > 1.0f)
		glossTop = 1.0f;
	style.glossTop = WithAlpha(white, glossTop);
	style.glossBottom = WithAlpha(white, glossBottom);
	return style;
}


/*!	The opaque color of a pill at \a fraction of its height (0 top, 1 bottom)
	along its vertical center line: the fill gradient with the gloss over its
	upper part. Drawing uses the same gradient stops.
*/
inline Color
PillColorAt(const PillStyle& style, float fraction)
{
	if (fraction < 0.0f)
		fraction = 0.0f;
	if (fraction > 1.0f)
		fraction = 1.0f;
	Color color = Mix(style.top, style.bottom, fraction);
	if (fraction <= kGlossExtent) {
		float t = fraction / kGlossExtent;
		float alpha = (style.glossTop.alpha
			+ (style.glossBottom.alpha - style.glossTop.alpha) * t) / 255.0f;
		color = Over(color, WithAlpha(MakeColor(255, 255, 255), alpha));
	}
	return color;
}


//!	The lowest contrast between the label and the pill behind it.
inline double
PillLabelContrast(const PillStyle& style)
{
	double lowest = 21.0;
	for (int i = 0; i <= 10; i++) {
		float fraction = kLabelTop + (kLabelBottom - kLabelTop) * i / 10.0f;
		double ratio = ContrastRatio(style.text, PillColorAt(style, fraction));
		if (ratio < lowest)
			lowest = ratio;
	}
	return lowest;
}


/*!	Contrast of the pill's outline against the window background: the
	larger of the border's and the fill's, as either makes the control's
	extent visible (WCAG 1.4.11).
*/
inline double
PillBoundaryContrast(const PillStyle& style, Color background)
{
	double border = ContrastRatio(style.border, background);
	double fill = ContrastRatio(Mix(style.top, style.bottom, 0.5f),
		background);
	return border > fill ? border : fill;
}


/*!	The highlight of a selected menu item, on the selected-item background
	\a base with \a text: a glossy rounded bar, gentler than a button.
*/
inline PillStyle
MenuHighlightStyle(const Palette& palette, Color base, Color text)
{
	PillStyle style = PillStyleFor(palette, kNeutralPill, 0, base, text,
		palette.menuBackground);
	style.top = Mix(base, MakeColor(255, 255, 255), 0.06f);
	style.bottom = Mix(base, MakeColor(0, 0, 0), 0.10f);
	style.border = Mix(base, MakeColor(0, 0, 0), 0.25f);
	style.glossTop = WithAlpha(MakeColor(255, 255, 255), 0.14f);
	style.glossBottom = WithAlpha(MakeColor(255, 255, 255), 0.0f);
	style.shadowOffset = 0;
	return style;
}


/*!	The satin gradient of a menu bar on the menu background \a base: lighter
	at the top, a step toward satin (or darker) at the bottom.
*/
inline void
MenuBarColors(const Palette& palette, Color base, Color& top, Color& bottom)
{
	if (IsDarkColor(base)) {
		top = Mix(base, MakeColor(255, 255, 255), 0.07f);
		bottom = Mix(base, MakeColor(0, 0, 0), 0.18f);
	} else {
		top = Mix(base, MakeColor(255, 255, 255), 0.60f);
		bottom = Mix(base, palette.satin, 0.45f);
	}
}


// #pragma mark - window controls


//!	The glyph ink drawn on a window control of fill \a color.
inline Color
WindowControlInk(Color color)
{
	return Mix(color, MakeColor(0, 0, 0), 0.68f);
}


//!	The rim around a window control of fill \a color.
inline Color
WindowControlRim(Color color)
{
	return Mix(color, MakeColor(0, 0, 0), 0.40f);
}


/*!	Contrast of a window control's extent against the title area color
	\a title: the larger of the fill's and the rim's.
*/
inline double
WindowControlContrast(Color color, Color title)
{
	double fill = ContrastRatio(color, title);
	double rim = ContrastRatio(WindowControlRim(color), title);
	return fill > rim ? fill : rim;
}


//!	The title area color at \a fraction of its height, without pinstripes.
inline Color
TitleColorAt(const Palette& palette, bool active, float fraction)
{
	return active
		? Mix(palette.titleTop, palette.titleBottom, fraction)
		: Mix(palette.inactiveTitleTop, palette.inactiveTitleBottom, fraction);
}


// #pragma mark - geometry


struct Rect {
	float		left;
	float		top;
	float		right;
	float		bottom;

	float		Width() const { return right - left; }
	float		Height() const { return bottom - top; }
	bool		IsValid() const { return left <= right && top <= bottom; }
};


inline Rect
MakeRect(float left, float top, float right, float bottom)
{
	Rect rect = { left, top, right, bottom };
	return rect;
}


inline Rect
InsetRect(Rect rect, float dx, float dy)
{
	return MakeRect(rect.left + dx, rect.top + dy, rect.right - dx,
		rect.bottom - dy);
}


enum Shape {
	kCapsuleShape = 0,
		// fully rounded ends: push buttons
	kRoundedShape,
		// small radius: square tool buttons, menu fields, check boxes
	kSquareShape,
		// straight edges: parts of a larger control (segments, arrows)
	kFlatShape
		// no surface: flat tool bar buttons at rest
};


static const float kRoundedRadius = 4.0f;
static const float kCapsuleMinAspect = 1.4f;
	// narrower controls look like blobs as capsules


/*!	Which shape a button of \a frame gets. \a allBorders is false for
	buttons that are part of a larger control (segments, spinner arrows,
	scroll bar arrows); those keep straight edges. \a flatAtRest is true for
	flat buttons that are not hovered, pressed or focused.
*/
inline Shape
ShapeFor(Rect frame, bool allBorders, bool flatAtRest)
{
	if (flatAtRest)
		return kFlatShape;
	if (!allBorders)
		return kSquareShape;
	float width = frame.Width() + 1;
	float height = frame.Height() + 1;
	if (height <= 0 || width < height * kCapsuleMinAspect)
		return kRoundedShape;
	return kCapsuleShape;
}


//!	The corner radius of a body of \a shape that fills \a body.
inline float
CornerRadius(Shape shape, Rect body)
{
	float width = body.Width() + 1;
	float height = body.Height() + 1;
	float half = (width < height ? width : height) / 2;
	if (half < 0)
		return 0;
	switch (shape) {
		case kCapsuleShape:
			return half;
		case kRoundedShape:
			return kRoundedRadius < half ? kRoundedRadius : half;
		default:
			return 0;
	}
}


struct PillGeometry {
	Rect		body;			// the filled, bordered surface
	float		radius;
	Rect		gloss;			// the highlight over the upper part
	float		glossRadius;
	Rect		focusRing;		// stroked with a 2 unit pen
	float		focusRadius;
};


/*!	The gloss highlight of a convex surface \a body with corner \a radius:
	inside the border, away from the rounded ends, over the upper part, so
	it reads as a reflection. \a gloss is invalid when the body is too
	small for one.
*/
inline void
GlossFor(Rect body, float radius, Rect& gloss, float& glossRadius)
{
	float height = body.Height() + 1;
	float sideInset = 1.0f + radius * 0.45f;
	gloss = MakeRect(body.left + sideInset, body.top + 1.0f,
		body.right - sideInset, body.top + floorf(height * kGlossExtent));
	float glossHalf = (gloss.Height() + 1) / 2;
	glossRadius = radius < glossHalf ? radius : glossHalf;
	if (glossRadius < 0)
		glossRadius = 0;
}


/*!	Lays out a pill in \a frame, which has \a margin units on each side for
	the focus ring and the shadow (the control look's frame inset).
*/
inline PillGeometry
PillGeometryFor(Rect frame, float margin, Shape shape)
{
	PillGeometry geometry;
	geometry.body = InsetRect(frame, margin, margin);
	if (!geometry.body.IsValid())
		geometry.body = frame;
	geometry.radius = CornerRadius(shape, geometry.body);
	GlossFor(geometry.body, geometry.radius, geometry.gloss,
		geometry.glossRadius);

	// the focus ring hugs the body, in the margin
	float ringInset = margin > 1.0f ? margin - 1.0f : 0.0f;
	geometry.focusRing = InsetRect(frame, ringInset, ringInset);
	geometry.focusRadius = geometry.radius > 0
		? geometry.radius + (margin - ringInset) : 0;
	return geometry;
}


/*!	How many whole units to cut from the edge, on row \a row (0 is the
	outermost) of a rounded corner of \a radius, so that the rest of the row
	lies inside the quarter circle. Used for the decorator's rounded title
	bar corners, which are made by leaving those units out of the
	decorator's footprint: the windows below then draw there, so no stale
	pixels stay behind.
*/
inline int32_t
CornerCut(float radius, int32_t row)
{
	if (radius <= 0 || row < 0 || row >= (int32_t)radius)
		return 0;
	float dy = radius - (row + 0.5f);
	float dx = sqrtf(radius * radius - dy * dy);
	int32_t cut = (int32_t)floorf(radius - dx + 0.5f);
	return cut < 0 ? 0 : cut;
}


}	// namespace KoiPop


#endif	// _KOIPOP_THEME_H
