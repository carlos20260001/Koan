/*
 * Copyright 2026, Koan contributors. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef MODERN_TAB_DECORATOR_H
#define MODERN_TAB_DECORATOR_H


#include "DecorManager.h"
#include "SATDecorator.h"
#include "TabStripLayout.h"


class Desktop;


class ModernTabDecorAddOn : public DecorAddOn {
public:
								ModernTabDecorAddOn(image_id id,
									const char* name);

protected:
	virtual	Decorator*			_AllocateDecorator(DesktopSettings& settings,
									BRect rect, Desktop* desktop);
};


/*!	Window decorator that shows the windows of a native window stack as a tab
	strip in the title bar.

	Each tab is one independent native window; the stack itself, its order
	and its active (top layer) member stay in the window stack and the Stack
	& Tile group model. This class only lays out, draws and hit-tests:

	- window controls (close, minimize, zoom) at the left, acting on the
	  stack's top window;
	- a strip of tabs with adaptive widths, a minimum width, and edge stubs
	  for tabs that do not fit;
	- a close button inside each tab that is wide enough (always on the
	  active tab).

	Dragging, reordering, detaching and joining are driven by the window
	behaviour, see HasTabStrip(). All geometry is in logical coordinates.
*/
class ModernTabDecorator : public SATDecorator {
public:
								ModernTabDecorator(DesktopSettings& settings,
									BRect frame, Desktop* desktop);
	virtual						~ModernTabDecorator();

	virtual	void				Draw(BRect updateRect);
	virtual	void				Draw();

	virtual	Region				RegionAt(BPoint where, int32& tab) const;
	virtual	bool				SetRegionHighlight(Region region,
									uint8 highlight, BRegion* dirty,
									int32 tab = -1);
	virtual	void				ExtendDirtyRegion(Region region,
									BRegion& dirty);

	virtual	bool				HasTabStrip() const { return true; }
	virtual	int32				TabStripDropIndex(int32 tab,
									const BPoint& where) const;

	virtual	bool				MoveTab(int32 from, int32 to, bool isMoving,
									BRegion* updateRegion = NULL);

	virtual	void				UpdateColors(DesktopSettings& settings);

protected:
	struct StripTab : public Decorator::Tab {
								StripTab();

			BRect				tabCloseRect;
				// close button inside the tab, invalid when not shown
			bool				isStub;
				// drawn as an overflow stub at the strip's edge
	};

	virtual	Decorator::Tab*		_AllocateNewTab();

	virtual	void				_DoLayout();

	virtual	void				_DrawFrame(BRect invalid);
	virtual	void				_DrawTabs(BRect invalid);
	virtual	void				_DrawTab(Decorator::Tab* tab, BRect invalid);
	virtual	void				_DrawTitle(Decorator::Tab* tab, BRect rect);
	virtual	void				_DrawButtons(Decorator::Tab* tab,
									const BRect& invalid);
	virtual	void				_DrawClose(Decorator::Tab* tab, bool direct,
									BRect rect);
	virtual	void				_DrawMinimize(Decorator::Tab* tab,
									bool direct, BRect rect);
	virtual	void				_DrawZoom(Decorator::Tab* tab, bool direct,
									BRect rect);

	virtual	void				_SetTitle(Decorator::Tab* tab,
									const char* string,
									BRegion* updateRegion = NULL);
	virtual	void				_SetFocus(Decorator::Tab* tab);
	virtual	bool				_SetTabLocation(Decorator::Tab* tab,
									float location, bool isShifting,
									BRegion* updateRegion = NULL);

	virtual	void				_MoveBy(BPoint offset);
	virtual	void				_ResizeBy(BPoint offset, BRegion* dirty);

	virtual	bool				_AddTab(DesktopSettings& settings,
									int32 index = -1,
									BRegion* updateRegion = NULL);
	virtual	bool				_RemoveTab(int32 index,
									BRegion* updateRegion = NULL);
	virtual	bool				_MoveTab(int32 from, int32 to, bool isMoving,
									BRegion* updateRegion = NULL);

	virtual	void				_GetFootprint(BRegion* region);
	virtual	void				_UpdateFont(DesktopSettings& settings);
	virtual	void				_TopTabChanged();

private:
			StripTab*			_StripTabAt(int32 index) const;
			bool				_EnsureSlots(int32 count);
			void				_UpdatePalette(DesktopSettings& settings);

			void				_DrawStripBackground(BRect invalid);
			void				_DrawStub(StripTab* tab);
			void				_DrawTabCloseButton(StripTab* tab);
			void				_DrawControl(Decorator::Tab* tab, BRect rect,
									rgb_color color, int32 glyph,
									bool pressed, bool enabled);
			void				_DrawSeparatorAfter(int32 index);

			bool				_IsActive(const Decorator::Tab* tab) const
									{ return tab == fTopTab; }
			bool				_IsFocusedStack() const;

private:
			TabStripMetrics		fMetrics;
			TabStripSlot*		fSlots;
			int32				fSlotCapacity;
			int32				fFirstVisibleTab;

			float				fStripHeight;

	mutable	bool				fCloseHitInTab;
				// the last close button hit was a tab's, not the control

			// palette, derived from the system colors in UpdateColors()
			rgb_color			fStripColor;
			rgb_color			fInactiveStripColor;
			rgb_color			fSurfaceColor;
			rgb_color			fTextColor;
			rgb_color			fInactiveTextColor;
			rgb_color			fSeparatorColor;
			rgb_color			fBorderColor;
			rgb_color			fInactiveBorderColor;
			rgb_color			fAccentColor;
			rgb_color			fHighlightColor;
			rgb_color			fCloseColor;
			rgb_color			fMinimizeColor;
			rgb_color			fZoomColor;
};


#endif	// MODERN_TAB_DECORATOR_H
