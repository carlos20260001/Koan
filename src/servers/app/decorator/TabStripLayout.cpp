/*
 * Copyright 2026, Koan contributors. All rights reserved.
 * Distributed under the terms of the MIT License.
 */


#include "TabStripLayout.h"

#include <math.h>


static void
place_tabs(const TabStripMetrics& metrics, TabStripSlot* slots,
	int32_t first, int32_t count, int32_t activeIndex, float start,
	float width, float gap)
{
	if (width < 0.0f)
		width = 0.0f;

	for (int32_t k = 0; k < count; k++) {
		TabStripSlot& slot = slots[first + k];
		float origin = start + k * (width + gap);
		slot.left = roundf(origin);
		slot.right = roundf(origin + width);
		slot.visible = slot.right > slot.left;
		slot.stub = false;

		float slotWidth = slot.right - slot.left;
		if (first + k == activeIndex)
			slot.showsCloseButton = slot.visible
				&& slotWidth >= metrics.minTabWidth;
		else {
			slot.showsCloseButton = slot.visible
				&& slotWidth >= metrics.closeButtonMinTabWidth;
		}
	}
}


static void
place_stub(TabStripSlot& slot, float left, float right)
{
	slot.left = roundf(left);
	slot.right = roundf(right);
	slot.visible = slot.right > slot.left;
	slot.stub = true;
	slot.showsCloseButton = false;
}


int32_t
TabStripLayout::Layout(const TabStripMetrics& metrics, int32_t count,
	float left, float right, int32_t activeIndex, int32_t firstVisible,
	TabStripSlot* slots)
{
	if (count <= 0 || slots == NULL)
		return 0;

	for (int32_t i = 0; i < count; i++) {
		slots[i].left = 0.0f;
		slots[i].right = 0.0f;
		slots[i].visible = false;
		slots[i].stub = false;
		slots[i].showsCloseButton = false;
	}

	if (activeIndex < 0)
		activeIndex = 0;
	if (activeIndex >= count)
		activeIndex = count - 1;

	const float gap = metrics.tabGap;
	float available = right - left;
	if (available < 0.0f)
		available = 0.0f;

	// Everything fits: share the strip, up to the maximum width per tab.
	float needed = count * metrics.minTabWidth + (count - 1) * gap;
	if (needed <= available) {
		float width = (available - (count - 1) * gap) / count;
		if (width > metrics.maxTabWidth)
			width = metrics.maxTabWidth;
		place_tabs(metrics, slots, 0, count, activeIndex, left, width, gap);
		return 0;
	}

	// Overflow: show a run of tabs at the minimum width or more, with a stub
	// on each side that has hidden tabs.
	const float stub = metrics.overflowStubWidth;
	float inner = available - 2 * (stub + gap);
	int32_t shown = (int32_t)floorf((inner + gap) / (metrics.minTabWidth + gap));
	if (shown < 1)
		shown = 1;
	if (shown > count)
		shown = count;

	if (firstVisible > count - shown)
		firstVisible = count - shown;
	if (firstVisible < 0)
		firstVisible = 0;
	if (activeIndex < firstVisible)
		firstVisible = activeIndex;
	if (activeIndex >= firstVisible + shown)
		firstVisible = activeIndex - shown + 1;

	float start = left;
	float end = right;
	if (firstVisible > 0) {
		place_stub(slots[firstVisible - 1], left, left + stub);
		start = left + stub + gap;
	}
	if (firstVisible + shown < count) {
		place_stub(slots[firstVisible + shown], right - stub, right);
		end = right - stub - gap;
	}

	float width = (end - start - (shown - 1) * gap) / shown;
	if (width > metrics.maxTabWidth)
		width = metrics.maxTabWidth;
	place_tabs(metrics, slots, firstVisible, shown, activeIndex, start, width,
		gap);

	return firstVisible;
}


int32_t
TabStripLayout::SlotAt(const TabStripSlot* slots, int32_t count, float x)
{
	for (int32_t i = 0; i < count; i++) {
		if (slots[i].visible && x >= slots[i].left && x < slots[i].right)
			return i;
	}
	return -1;
}


int32_t
TabStripLayout::DropIndex(const TabStripSlot* slots, int32_t count,
	int32_t movingIndex, float x)
{
	if (slots == NULL || movingIndex < 0 || movingIndex >= count)
		return movingIndex;

	int32_t target = movingIndex;
	for (int32_t i = movingIndex - 1; i >= 0; i--) {
		if (!slots[i].visible || slots[i].stub)
			break;
		if (x >= (slots[i].left + slots[i].right) / 2)
			break;
		target = i;
	}
	if (target != movingIndex)
		return target;

	for (int32_t i = movingIndex + 1; i < count; i++) {
		if (!slots[i].visible || slots[i].stub)
			break;
		if (x < (slots[i].left + slots[i].right) / 2)
			break;
		target = i;
	}
	return target;
}


bool
TabStripLayout::ShouldDetach(float top, float bottom, float y,
	float threshold)
{
	return y < top - threshold || y > bottom + threshold;
}
