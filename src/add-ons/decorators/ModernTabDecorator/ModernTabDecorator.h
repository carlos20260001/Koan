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
	  active tab), and on any tab while the pointer is over it;
	- hover states (HIGHLIGHT_HOVER) for tabs, tab close buttons, window
	  controls and overflow stubs;
	- the dragged tab following the pointer while the others make room
	  (SetDraggedTab());
	- rounded window corners (GetCornerCutouts()), square while the window
	  spans a display or is tiled (SetSquareCorners()).

	Dragging, reordering, detaching and joining are driven by the window
	behaviour, see HasTabStrip(). All geometry is in logical coordinates.

	Themes can subclass it and replace the palette (_UpdatePalette()) and
	the painting of the strip, the tab surfaces, the stubs, the tab close
	buttons and the window controls, keeping the layout, hit testing, hover
	and drag behaviour.
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
	virtual	int32				SetDraggedTab(int32 tab, float grabOffset,
									float pointerX, BRegion* dirty);

	virtual	bool				MoveTab(int32 from, int32 to, bool isMoving,
									BRegion* updateRegion = NULL);

	virtual	void				GetCornerCutouts(BRegion& region) const;
	virtual	bool				SetSquareCorners(bool square);

	virtual	void				UpdateColors(DesktopSettings& settings);

protected:
	struct StripTab : public Decorator::Tab {
								StripTab();

			BRect				tabCloseRect;
				// close button inside the tab, invalid when not shown
			bool				isStub;
				// drawn as an overflow stub at the strip's edge
			bool				closeAlwaysShown;
				// otherwise tabCloseRect is only shown (and hit) while
				// the pointer is over the tab
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

	// painting hooks for themes
	enum {
		GLYPH_CLOSE,
		GLYPH_MINIMIZE,
		GLYPH_ZOOM
	};

	virtual	void				_UpdatePalette(DesktopSettings& settings);
	virtual	void				_DrawStripBackground(BRect invalid);
	virtual	void				_DrawTabSurface(StripTab* tab, BRect rect,
									bool active, bool focused,
									bool hovered);
	virtual	void				_DrawStub(StripTab* tab);
	virtual	void				_DrawTabCloseButton(StripTab* tab);
	virtual	void				_DrawControl(Decorator::Tab* tab, BRect rect,
									rgb_color color, int32 glyph,
									bool pressed, bool enabled,
									bool hovered);
	virtual	void				_DrawSeparatorAfter(int32 index);

			StripTab*			_StripTabAt(int32 index) const;
			bool				_IsHovered(int32 index) const;
			bool				_IsCloseShown(const StripTab* tab) const;
			bool				_IsActive(const Decorator::Tab* tab) const
									{ return tab == fTopTab; }
			bool				_IsFocusedStack() const;

			// rounded corners (cut out of the footprint by _UpdateCorners())
			BRect				_OuterBounds() const;
			int32				_CornerRadius() const
									{ return fCornerRadius; }
			void				_DrawCorner(BRect bounds, bool right,
									bool bottom, rgb_color fill,
									rgb_color border);

private:
			bool				_EnsureSlots(int32 count);
			void				_PlaceTabs();
			void				_IncludeHoverRects(BRegion* dirty,
									Region region, int32 tab) const;
			void				_UpdateCorners();

private:
			TabStripMetrics		fMetrics;
			TabStripSlot*		fSlots;
			int32				fSlotCapacity;
			int32				fFirstVisibleTab;

			float				fStripHeight;

	mutable	bool				fCloseHitInTab;
				// the last close button hit was a tab's, not the control

			// hover state, see SetRegionHighlight()
			Region				fHoverRegion;
			int32				fHoverTab;
			bool				fHoverInTabClose;

			// rounded corners
			bool				fSquareCorners;
			float				fCornerScale;
				// the render scale fCornerCutouts was made for
			int32				fCornerRadius;
				// 0 for square corners
			BRegion				fCornerCutouts;

			// tab drag state, see SetDraggedTab()
			int32				fDragTab;
			float				fDragGrabOffset;
			float				fDragPointerX;
			float				fDraggedLeft;
			int32				fDropIndex;
			float*				fDragOffsets;

protected:
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
