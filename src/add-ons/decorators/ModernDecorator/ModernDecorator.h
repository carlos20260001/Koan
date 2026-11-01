/*
 * Copyright 2026, Koan contributors
 * SPDX-License-Identifier: Apache-2.0
 */


#ifndef MODERN_DECORATOR_H
#define MODERN_DECORATOR_H


#include "Decorator.h"
#include "SATDecorator.h"


class Desktop;
class ServerBitmap;


class ModernDecorAddOn : public DecorAddOn {
public:
								ModernDecorAddOn(image_id id, const char* name);

protected:
	virtual Decorator*			_AllocateDecorator(DesktopSettings& settings,
									BRect rect, Desktop* desktop);
};


/*! Modern Cuppertino-inspired window decorator.

    Provides traffic light window controls (close, minimize, maximize) at the
    left of the title bar, a Cuppertino-inspired color scheme, and a modern tab
    chrome. Subclasses SATDecorator so stack-and-tile behavior is preserved.
 */
class ModernDecorator : public SATDecorator {
public:
								ModernDecorator(DesktopSettings& settings,
									BRect frame, Desktop* desktop);
	virtual						~ModernDecorator();

	virtual	void				Draw(BRect updateRect);
	virtual	void				Draw();

	virtual	Region				RegionAt(BPoint where, int32& tab) const;
	virtual	bool				SetRegionHighlight(Region region,
									uint8 highlight, BRegion* dirty,
									int32 tab = -1);

	virtual	void				UpdateColors(DesktopSettings& settings);

protected:
	virtual	void				_DoLayout();
	virtual	void				_DrawFrame(BRect rect);
	virtual	void				_DrawTab(Decorator::Tab* tab, BRect invalid);
	virtual	void				_DrawTitle(Decorator::Tab* tab, BRect rect);
	virtual	void				_DrawButtons(Decorator::Tab* tab,
									const BRect& invalid);
	virtual	void				_DrawClose(Decorator::Tab* tab, bool direct,
									BRect rect);
	virtual	void				_DrawMinimize(Decorator::Tab* tab, bool direct,
									BRect rect);
	virtual	void				_DrawZoom(Decorator::Tab* tab, bool direct,
									BRect rect);

	virtual	void				_SetTitle(Tab* tab, const char* string,
									BRegion* updateRegion = NULL);
	virtual	void				_MoveBy(BPoint offset);
	virtual	void				_ResizeBy(BPoint offset, BRegion* dirty);
	virtual	Decorator::Tab*		_AllocateNewTab();
	virtual	bool				_AddTab(DesktopSettings& settings,
									int32 index = -1,
									BRegion* updateRegion = NULL);
	virtual	bool				_RemoveTab(int32 index,
									BRegion* updateRegion = NULL);
	virtual	bool				_MoveTab(int32 from, int32 to, bool isMoving,
									BRegion* updateRegion = NULL);
	virtual	void				_GetFootprint(BRegion* region);
	virtual	void				_UpdateFont(DesktopSettings& settings);

private:
	void						_DrawTrafficLight(Decorator::Tab* tab,
									BRect rect, bool pressed, bool highlighted,
									rgb_color color);

	rgb_color					fCloseColor;      // traffic light red
	rgb_color					fMinimizeColor;   // traffic light amber
	rgb_color					fZoomColor;       // traffic light green
	rgb_color					fFrameColor;      // title bar background
	rgb_color					fFocusTextColor;
	rgb_color					fNonFocusTextColor;
};


#endif	// MODERN_DECORATOR_H
