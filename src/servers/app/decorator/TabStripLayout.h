/*
 * Copyright 2026, Koan contributors. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef _TAB_STRIP_LAYOUT_H
#define _TAB_STRIP_LAYOUT_H


#include <stdint.h>


/*!	Geometry of a tab strip: the row of tabs a tab-strip decorator draws for
	the windows of one native window stack.

	All values are logical coordinates (the decorator's coordinate space);
	app_server scales them only when it rasterizes. Edges are rounded to whole
	logical units once, here, so adjacent tabs share an edge and hit testing
	matches drawing exactly.

	The code is plain C++ so a host test can cover it without the window
	server.
*/


struct TabStripMetrics {
	float				minTabWidth;
		// tabs never get narrower; more tabs overflow instead
	float				maxTabWidth;
		// tabs never get wider, the rest of the strip stays empty
	float				tabGap;
		// space between two tabs
	float				overflowStubWidth;
		// width of the edge stub that stands for hidden tabs
	float				closeButtonMinTabWidth;
		// an inactive tab narrower than this shows no close button
};


struct TabStripSlot {
	float				left;
	float				right;
		// half-open span [left, right); empty when !visible
	bool				visible;
	bool				stub;
		// a narrow edge stub standing for this hidden tab
	bool				showsCloseButton;
};


class TabStripLayout {
public:
	/*!	Lays out \a count tabs in the span [\a left, \a right).

		When all tabs fit at minTabWidth they share the span, each at most
		maxTabWidth wide. Otherwise the strip overflows: a run of tabs starting
		at \a firstVisible is shown at full size, and the nearest hidden tab on
		each side is drawn as a narrow stub, so every tab stays reachable by
		repeatedly clicking a stub (and by keyboard). The run always contains
		\a activeIndex.

		\param slots receives \a count entries.
		\return the first fully visible tab; pass it back as \a firstVisible
			on the next call to keep the strip from jumping.
	*/
	static	int32_t				Layout(const TabStripMetrics& metrics,
									int32_t count, float left, float right,
									int32_t activeIndex, int32_t firstVisible,
									TabStripSlot* slots);

	/*!	Returns the index of the slot under \a x, or -1. */
	static	int32_t				SlotAt(const TabStripSlot* slots,
									int32_t count, float x);

	/*!	Returns the stack position a tab dragged from \a movingIndex should
		take while the pointer is at \a x. A tab moves past a neighbour once
		the pointer crosses the middle of that neighbour, which keeps the
		reorder stable while the pointer hovers on a boundary. Hidden tabs are
		skipped.
	*/
	static	int32_t				DropIndex(const TabStripSlot* slots,
									int32_t count, int32_t movingIndex,
									float x);

	/*!	Geometry while the tab at \a movingIndex is dragged along the strip.

		The dragged tab follows the pointer: its left edge is \a pointerX
		minus \a grabOffset (where the tab was grabbed), clamped so the tab
		stays within the shown (non-stub) tabs. The dragged tab passes a
		neighbour once its leading edge crosses the middle of that
		neighbour; that gives the drop slot, and every tab between the
		dragged tab's own slot and the drop slot moves aside by one slot to
		show where it will land.

		\param draggedLeft receives the dragged tab's left edge.
		\param offsets if not \c NULL, receives \a count horizontal shifts to
			add to the slots; the dragged tab's own entry is 0.
		\return the drop slot, i.e. the stack position the tab takes on
			release; \a movingIndex when it cannot be dragged.
	*/
	static	int32_t				DragLayout(const TabStripSlot* slots,
									int32_t count, int32_t movingIndex,
									float grabOffset, float pointerX,
									float* draggedLeft, float* offsets);

	/*!	Returns whether a tab dragged vertically to \a y has left the strip
		spanning [\a top, \a bottom] by more than \a threshold, i.e. whether
		the drag should detach the window from its stack.
	*/
	static	bool				ShouldDetach(float top, float bottom,
									float y, float threshold);
};


#endif	// _TAB_STRIP_LAYOUT_H
