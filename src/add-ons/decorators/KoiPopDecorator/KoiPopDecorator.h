/*
 * Copyright 2026, Koan contributors. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef KOIPOP_DECORATOR_H
#define KOIPOP_DECORATOR_H


#include "ModernTabDecorator.h"

#include <KoiPopTheme.h>


class KoiPopDecorAddOn : public DecorAddOn {
public:
								KoiPopDecorAddOn(image_id id,
									const char* name);

protected:
	virtual	Decorator*			_AllocateDecorator(DesktopSettings& settings,
									BRect rect, Desktop* desktop);
};


/*!	Window decorator of the KoiPop glossy desktop theme.

	It is the tab strip decorator (ModernTabDecorator) with KoiPop painting:
	a satin metal title area with fine pinstripes, a raised glossy active
	tab with an ocean blue accent, and glossy red/amber/green window
	controls with a rim and an always visible glyph. Layout, hit testing,
	hover, tab dragging, native window stacking and the rounded, cut-out
	window corners all come from the tab strip decorator unchanged.

	Colors come from KoiPopTheme.h, in the appearance (light or dark) of
	the panel background color; the active tab and its title use the
	system panel colors, so they always join the window contents.
*/
class KoiPopDecorator : public ModernTabDecorator {
public:
								KoiPopDecorator(DesktopSettings& settings,
									BRect frame, Desktop* desktop);
	virtual						~KoiPopDecorator();

protected:
	virtual	void				_UpdatePalette(DesktopSettings& settings);
	virtual	void				_DrawStripBackground(BRect invalid);
	virtual	void				_DrawTabSurface(StripTab* tab, BRect rect,
									bool active, bool focused,
									bool hovered);
	virtual	void				_DrawTitle(Decorator::Tab* tab, BRect rect);
	virtual	void				_DrawControl(Decorator::Tab* tab, BRect rect,
									rgb_color color, int32 glyph,
									bool pressed, bool enabled,
									bool hovered);

private:
			const KoiPop::Palette* fPalette;
};


#endif	// KOIPOP_DECORATOR_H
