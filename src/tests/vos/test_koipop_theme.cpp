/*
 * Copyright 2026, Koan contributors. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

// Host test for the KoiPop theme tokens: WCAG AA contrast of the light and
// dark palettes, the control state mapping and the capsule geometry. Runs
// anywhere with a C++ compiler, no window server required.

#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#include "KoiPopTheme.h"


using namespace KoiPop;


static int sFailures = 0;
static bool sVerbose = false;


#define CHECK(expression) \
	do { \
		if (!(expression)) { \
			printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #expression); \
			sFailures++; \
		} \
	} while (0)


static const char*
appearance_name(Appearance appearance)
{
	return appearance == kDarkAppearance ? "dark" : "light";
}


static void
check_contrast(Appearance appearance, const char* what, Color a, Color b,
	double minimum)
{
	double ratio = ContrastRatio(a, b);
	if (sVerbose)
		printf("  %-5s %-44s %5.2f\n", appearance_name(appearance), what, ratio);
	if (ratio + 1e-9 < minimum) {
		printf("FAIL %s: %s contrast %.2f < %.1f\n",
			appearance_name(appearance), what, ratio, minimum);
		sFailures++;
	}
}


static void
check_value(Appearance appearance, const char* what, double ratio,
	double minimum)
{
	if (sVerbose)
		printf("  %-5s %-44s %5.2f\n", appearance_name(appearance), what, ratio);
	if (ratio + 1e-9 < minimum) {
		printf("FAIL %s: %s %.2f < %.1f\n", appearance_name(appearance), what,
			ratio, minimum);
		sFailures++;
	}
}


// #pragma mark - color math


static void
TestContrastMath()
{
	Color black = MakeColor(0, 0, 0);
	Color white = MakeColor(255, 255, 255);
	CHECK(fabs(ContrastRatio(black, white) - 21.0) < 0.01);
	CHECK(fabs(ContrastRatio(white, white) - 1.0) < 1e-9);
	CHECK(ContrastRatio(black, white) == ContrastRatio(white, black));
	// a known reference value: #767676 on white is 4.54:1
	CHECK(fabs(ContrastRatio(MakeColor(118, 118, 118), white) - 4.54) < 0.01);

	Color mid = Mix(black, white, 0.5f);
	CHECK(mid.red == 128 && mid.green == 128 && mid.blue == 128);
	CHECK(Mix(black, white, -1).red == 0);
	CHECK(Mix(black, white, 2).red == 255);
	CHECK(Over(black, WithAlpha(white, 0)).red == 0);
	CHECK(Over(black, WithAlpha(white, 1)).red == 255);

	Color gray = Desaturate(MakeColor(200, 40, 40), 1.0f);
	CHECK(gray.red == gray.green && gray.green == gray.blue);
}


// #pragma mark - palette contrast


static void
TestPaletteText(Appearance appearance)
{
	const Palette& p = GetPalette(appearance);
	const double text = kMinTextContrast;

	check_contrast(appearance, "window text / window", p.windowText,
		p.windowBackground, text);
	check_contrast(appearance, "secondary text / window", p.secondaryText,
		p.windowBackground, text);
	check_contrast(appearance, "document text / document", p.documentText,
		p.documentBackground, text);
	check_contrast(appearance, "link / document", p.linkText,
		p.documentBackground, text);
	check_contrast(appearance, "link / window", p.linkText,
		p.windowBackground, text);
	check_contrast(appearance, "visited link / document", p.linkVisitedText,
		p.documentBackground, text);
	check_contrast(appearance, "visited link / window", p.linkVisitedText,
		p.windowBackground, text);
	check_contrast(appearance, "control text / control", p.controlText,
		p.controlBackground, text);
	check_contrast(appearance, "menu text / menu", p.menuText,
		p.menuBackground, text);
	check_contrast(appearance, "selected menu text / selected menu",
		p.menuSelectedText, p.menuSelectedBackground, text);
	check_contrast(appearance, "list text / list", p.listText,
		p.listBackground, text);
	check_contrast(appearance, "selected list text / selected list",
		p.listSelectedText, p.listSelectedBackground, text);
	check_contrast(appearance, "tool tip text / tool tip", p.toolTipText,
		p.toolTipBackground, text);
	check_contrast(appearance, "window text / active tab", p.windowText,
		p.tabSurface, text);

	// semantic fills
	check_contrast(appearance, "on info / info", p.onInfo, p.info, text);
	check_contrast(appearance, "on success / success", p.onSuccess,
		p.success, text);
	check_contrast(appearance, "on warning / warning", p.onWarning,
		p.warning, text);
	check_contrast(appearance, "on destructive / destructive",
		p.onDestructive, p.destructive, text);

	// title text over every part of the satin gradient, with and without
	// a pinstripe
	for (int i = 0; i <= 10; i++) {
		float fraction = i / 10.0f;
		Color active = TitleColorAt(p, true, fraction);
		Color inactive = TitleColorAt(p, false, fraction);
		check_contrast(appearance, "title text / title", p.titleText, active,
			text);
		check_contrast(appearance, "title text / pinstripe", p.titleText,
			Over(active, p.pinstripe), text);
		check_contrast(appearance, "inactive title text / title",
			p.inactiveTitleText, inactive, text);
		check_contrast(appearance, "inactive title text / pinstripe",
			p.inactiveTitleText, Over(inactive, p.pinstripe), text);
	}
}


static void
TestPaletteGraphics(Appearance appearance)
{
	const Palette& p = GetPalette(appearance);
	const double graphic = kMinGraphicContrast;

	check_contrast(appearance, "control border / window", p.controlBorder,
		p.windowBackground, graphic);
	check_contrast(appearance, "focus ring / window", p.focusRing,
		p.windowBackground, graphic);
	check_contrast(appearance, "focus ring / document", p.focusRing,
		p.documentBackground, graphic);
	check_contrast(appearance, "selection mark / window", p.selectionMark,
		p.windowBackground, graphic);

	// the progress bar fill against its track
	double progress = ContrastRatio(Mix(p.progressTop, p.progressBottom, 0.5f),
		p.trackBackground);
	check_value(appearance, "progress fill / track", progress, graphic);

	// window controls: extent against the title area, glyph against fill
	const Color controls[] = { p.closeControl, p.minimizeControl,
		p.zoomControl };
	for (int i = 0; i < 3; i++) {
		for (int j = 0; j <= 4; j++) {
			Color title = TitleColorAt(p, true, j / 4.0f);
			check_value(appearance, "window control / title",
				WindowControlContrast(controls[i], title), graphic);
		}
		check_contrast(appearance, "window control glyph / control",
			WindowControlInk(controls[i]), controls[i], graphic);
	}
}


// #pragma mark - pill states


static const uint32_t kEnabledStates[] = {
	0,
	kStateHover,
	kStatePressed,
	kStateFocused,
	kStateSelected,
	kStatePressed | kStateSelected,
	kStateHover | kStateFocused,
	kStateHover | kStateSelected,
	kStateFocused | kStateSelected
};


static void
TestPillStates(Appearance appearance)
{
	const Palette& p = GetPalette(appearance);
	const PillKind kinds[] = { kNeutralPill, kPrimaryPill, kDestructivePill };
	const char* kindNames[] = { "neutral", "primary", "destructive" };

	for (int k = 0; k < 3; k++) {
		for (size_t s = 0; s < sizeof(kEnabledStates) / sizeof(uint32_t);
				s++) {
			uint32_t state = kEnabledStates[s];
			PillStyle style = PillStyleFor(p, kinds[k], state,
				p.controlBackground, p.controlText, p.windowBackground);

			char what[96];
			snprintf(what, sizeof(what), "%s pill label, state 0x%02x",
				kindNames[k], (unsigned)state);
			check_value(appearance, what, PillLabelContrast(style),
				kMinTextContrast);

			snprintf(what, sizeof(what), "%s pill boundary, state 0x%02x",
				kindNames[k], (unsigned)state);
			check_value(appearance, what,
				PillBoundaryContrast(style, p.windowBackground),
				kMinGraphicContrast);

			CHECK(style.showFocusRing == ((state & kStateFocused) != 0));
			if (style.showFocusRing) {
				check_contrast(appearance, "pill focus ring / window",
					style.focusRing, p.windowBackground, kMinGraphicContrast);
			}

			// the selected state has a mark, not only another color
			CHECK(style.showMark == ((state & kStateSelected) != 0));
			if (style.showMark) {
				Color fill = PillColorAt(style, 0.85f);
				check_contrast(appearance, "selection mark / pill",
					style.mark, fill, kMinGraphicContrast);
			}

			// pressed reads as pushed in; only resting pills cast shadows
			CHECK(style.inset == ((state & kStatePressed) != 0));
			if (style.inset)
				CHECK(style.shadowOffset == 0);
			else
				CHECK(style.shadowOffset >= 1);
		}

		// hover is brighter than rest, pressed is darker
		PillStyle rest = PillStyleFor(p, kinds[k], 0, p.controlBackground,
			p.controlText, p.windowBackground);
		PillStyle hover = PillStyleFor(p, kinds[k], kStateHover,
			p.controlBackground, p.controlText, p.windowBackground);
		PillStyle pressed = PillStyleFor(p, kinds[k], kStatePressed,
			p.controlBackground, p.controlText, p.windowBackground);
		CHECK(RelativeLuminance(hover.top) >= RelativeLuminance(rest.top));
		CHECK(hover.shadowOffset > rest.shadowOffset);
		CHECK(RelativeLuminance(PillColorAt(pressed, 0.5f))
			< RelativeLuminance(PillColorAt(rest, 0.5f)));
		CHECK(pressed.glossTop.alpha < rest.glossTop.alpha);

		// disabled: desaturated, flat, no focus ring; WCAG exempts inactive
		// controls from the text minimum, but the label stays legible
		PillStyle disabled = PillStyleFor(p, kinds[k],
			kStateDisabled | kStateFocused | kStateHover, p.controlBackground,
			p.controlText, p.windowBackground);
		CHECK(!disabled.showFocusRing);
		CHECK(disabled.shadowOffset == 0);
		check_value(appearance, "disabled pill label (legibility)",
			PillLabelContrast(disabled), 2.0);
	}

	// the default action stands out by more than hue: a second outline
	// that contrasts with the window, and in the light appearance a much
	// darker fill with an inverted label
	PillStyle neutral = PillStyleFor(p, kNeutralPill, 0, p.controlBackground,
		p.controlText, p.windowBackground);
	PillStyle primary = PillStyleFor(p, kPrimaryPill, kStateDefault,
		p.controlBackground, p.controlText, p.windowBackground);
	CHECK(!neutral.showDefaultRing);
	CHECK(primary.showDefaultRing);
	check_contrast(appearance, "default ring / window", primary.defaultRing,
		p.windowBackground, kMinGraphicContrast);
	if (appearance == kLightAppearance) {
		CHECK(ContrastRatio(PillColorAt(neutral, 0.5f),
			PillColorAt(primary, 0.5f)) >= 3.0);
	}
}


// #pragma mark - menus


static void
TestMenus(Appearance appearance)
{
	const Palette& p = GetPalette(appearance);

	// the selected item's label over its whole glossy highlight
	PillStyle highlight = MenuHighlightStyle(p, p.menuSelectedBackground,
		p.menuSelectedText);
	check_value(appearance, "selected menu item label / highlight",
		PillLabelContrast(highlight), kMinTextContrast);

	// menu bar titles over the satin gradient
	Color top;
	Color bottom;
	MenuBarColors(p, p.menuBackground, top, bottom);
	for (int i = 0; i <= 4; i++) {
		check_contrast(appearance, "menu text / menu bar", p.menuText,
			Mix(top, bottom, i / 4.0f), kMinTextContrast);
	}
}


// #pragma mark - geometry


static void
TestShapes()
{
	// push buttons are capsules, square buttons and parts are rounded
	CHECK(ShapeFor(MakeRect(0, 0, 79, 23), true, false) == kCapsuleShape);
	CHECK(ShapeFor(MakeRect(0, 0, 23, 23), true, false) == kRoundedShape);
	CHECK(ShapeFor(MakeRect(0, 0, 79, 23), false, false) == kSquareShape);
	CHECK(ShapeFor(MakeRect(0, 0, 79, 23), true, true) == kFlatShape);
	CHECK(ShapeFor(MakeRect(0, 0, 10, -5), true, false) == kRoundedShape);
}


static void
TestCornerRadius()
{
	Rect wide = MakeRect(0, 0, 99, 23);
	CHECK(CornerRadius(kCapsuleShape, wide) == 12);
	CHECK(CornerRadius(kRoundedShape, wide) == kRoundedRadius);
	CHECK(CornerRadius(kFlatShape, wide) == 0);
	CHECK(CornerRadius(kSquareShape, wide) == 0);

	// never more than half of the shorter side
	Rect tiny = MakeRect(0, 0, 5, 5);
	CHECK(CornerRadius(kRoundedShape, tiny) == 3);
	CHECK(CornerRadius(kCapsuleShape, tiny) == 3);
	Rect tall = MakeRect(0, 0, 15, 99);
	CHECK(CornerRadius(kCapsuleShape, tall) == 8);
}


static bool
contains(Rect outer, Rect inner)
{
	return inner.left >= outer.left && inner.right <= outer.right
		&& inner.top >= outer.top && inner.bottom <= outer.bottom;
}


static void
TestPillGeometry()
{
	const float margins[] = { 2, 5 };
	const float offsets[] = { 0, 0.25f, 0.5f, 0.75f, 17.5f };
	const float heights[] = { 18, 23, 24, 31 };

	for (size_t m = 0; m < 2; m++) {
		for (size_t o = 0; o < 5; o++) {
			for (size_t h = 0; h < 4; h++) {
				Rect frame = MakeRect(offsets[o], offsets[o],
					offsets[o] + 119, offsets[o] + heights[h]);
				PillGeometry g = PillGeometryFor(frame, margins[m],
					kCapsuleShape);

				CHECK(g.body.IsValid());
				CHECK(contains(frame, g.body));
				CHECK(g.body.left - frame.left == margins[m]);
				CHECK(g.radius * 2 <= g.body.Height() + 1 + 1e-4);

				// the gloss stays in the upper half, inside the body
				CHECK(g.gloss.IsValid());
				CHECK(contains(g.body, g.gloss));
				CHECK(g.gloss.bottom - g.body.top
					<= (g.body.Height() + 1) / 2);
				CHECK(g.glossRadius * 2 <= g.gloss.Height() + 1 + 1e-4);

				// the focus ring is outside the body, inside the frame
				CHECK(contains(frame, g.focusRing));
				CHECK(contains(g.focusRing, g.body));
				CHECK(g.focusRing.left < g.body.left);

				// the same layout at any position: no drift from rounding
				Rect origin = MakeRect(0, 0, 119, heights[h]);
				PillGeometry r = PillGeometryFor(origin, margins[m],
					kCapsuleShape);
				CHECK(fabsf((g.gloss.left - frame.left)
					- (r.gloss.left - origin.left)) < 1e-4);
				CHECK(fabsf((g.gloss.bottom - frame.top)
					- (r.gloss.bottom - origin.top)) < 1e-4);
				CHECK(g.radius == r.radius);
			}
		}
	}

	// degenerate frames do not produce invalid bodies
	PillGeometry small = PillGeometryFor(MakeRect(0, 0, 3, 3), 5,
		kCapsuleShape);
	CHECK(small.body.IsValid());
	CHECK(small.radius >= 0);
}


static void
TestCornerCut()
{
	const float radii[] = { 3, 5, 6, 8 };
	for (size_t i = 0; i < 4; i++) {
		float radius = radii[i];
		int32_t previous = CornerCut(radius, 0);
		CHECK(previous >= 1);
		CHECK(previous < radius);
		for (int32_t row = 1; row < (int32_t)radius + 3; row++) {
			int32_t cut = CornerCut(radius, row);
			CHECK(cut <= previous);
			CHECK(cut >= 0);
			previous = cut;
		}
		CHECK(CornerCut(radius, (int32_t)radius) == 0);
	}
	CHECK(CornerCut(0, 0) == 0);
	CHECK(CornerCut(5, -1) == 0);
}


int
main(int argc, char** argv)
{
	sVerbose = argc > 1;

	TestContrastMath();
	for (int i = 0; i < 2; i++) {
		Appearance appearance = (Appearance)i;
		TestPaletteText(appearance);
		TestPaletteGraphics(appearance);
		TestPillStates(appearance);
		TestMenus(appearance);
	}
	TestShapes();
	TestCornerRadius();
	TestPillGeometry();
	TestCornerCut();

	if (sFailures != 0) {
		printf("%d check(s) failed\n", sFailures);
		return EXIT_FAILURE;
	}
	printf("test_koipop_theme: all checks passed\n");
	return EXIT_SUCCESS;
}
