/*
 * Copyright 2026, Koan contributors. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

// Host test for the tab strip geometry of tab-strip window decorators:
// runs anywhere with a C++ compiler, no window server required.

#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#include "TabStripLayout.h"


static int sFailures = 0;


#define CHECK(expression) \
	do { \
		if (!(expression)) { \
			printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #expression); \
			sFailures++; \
		} \
	} while (0)


static const TabStripMetrics kMetrics = {
	72.0f,		// minTabWidth
	220.0f,		// maxTabWidth
	2.0f,		// tabGap
	14.0f,		// overflowStubWidth
	96.0f		// closeButtonMinTabWidth
};


static int32_t
count_visible(const TabStripSlot* slots, int32_t count, bool stubs)
{
	int32_t visible = 0;
	for (int32_t i = 0; i < count; i++) {
		if (slots[i].visible && slots[i].stub == stubs)
			visible++;
	}
	return visible;
}


static void
TestSingleTabIsCapped()
{
	TabStripSlot slots[1];
	int32_t first = TabStripLayout::Layout(kMetrics, 1, 100, 1000, 0, 0,
		slots);

	CHECK(first == 0);
	CHECK(slots[0].visible);
	CHECK(!slots[0].stub);
	CHECK(slots[0].left == 100);
	CHECK(slots[0].right - slots[0].left == kMetrics.maxTabWidth);
	CHECK(slots[0].showsCloseButton);
}


static void
TestTabsShareTheStripWithoutGapsOrOverlap()
{
	// 3 tabs in 301 units: fractional widths must still tile the strip.
	TabStripSlot slots[3];
	TabStripLayout::Layout(kMetrics, 3, 10, 311, 1, 0, slots);

	for (int32_t i = 0; i < 3; i++) {
		CHECK(slots[i].visible);
		CHECK(slots[i].left == floorf(slots[i].left));
		CHECK(slots[i].right == floorf(slots[i].right));
		CHECK(slots[i].right - slots[i].left >= kMetrics.minTabWidth);
	}
	for (int32_t i = 1; i < 3; i++) {
		float gap = slots[i].left - slots[i - 1].right;
		CHECK(gap >= kMetrics.tabGap - 1 && gap <= kMetrics.tabGap + 1);
	}
	CHECK(slots[0].left == 10);
	CHECK(slots[2].right <= 311);
}


static void
TestFractionalStripEdges()
{
	// Strips that start and end on fractional logical positions (as at
	// 1.25x, 1.5x or 1.75x after the decorator's own arithmetic) must still
	// produce whole-unit, non-overlapping edges.
	const float starts[] = { 0.25f, 0.5f, 0.75f, 13.6f };
	for (size_t s = 0; s < sizeof(starts) / sizeof(starts[0]); s++) {
		TabStripSlot slots[4];
		TabStripLayout::Layout(kMetrics, 4, starts[s], starts[s] + 500.3f, 0,
			0, slots);
		for (int32_t i = 0; i < 4; i++) {
			CHECK(slots[i].left == roundf(slots[i].left));
			CHECK(slots[i].right == roundf(slots[i].right));
			if (i > 0)
				CHECK(slots[i].left >= slots[i - 1].right);
		}
	}
}


static void
TestNarrowInactiveTabsHideCloseButton()
{
	// 4 tabs in 340 units: about 83 each, below closeButtonMinTabWidth.
	TabStripSlot slots[4];
	TabStripLayout::Layout(kMetrics, 4, 0, 340, 2, 0, slots);

	CHECK(slots[2].showsCloseButton);
	CHECK(!slots[0].showsCloseButton);
	CHECK(!slots[1].showsCloseButton);
	CHECK(!slots[3].showsCloseButton);
}


static void
TestOverflowKeepsActiveTabVisible()
{
	const int32_t kCount = 20;
	TabStripSlot slots[kCount];

	for (int32_t active = 0; active < kCount; active++) {
		int32_t first = TabStripLayout::Layout(kMetrics, kCount, 0, 400,
			active, 0, slots);
		CHECK(slots[active].visible);
		CHECK(!slots[active].stub);
		CHECK(first <= active);

		// Every shown tab keeps the minimum width.
		for (int32_t i = 0; i < kCount; i++) {
			if (slots[i].visible && !slots[i].stub)
				CHECK(slots[i].right - slots[i].left >= kMetrics.minTabWidth);
		}

		// Stubs flank the run exactly where tabs are hidden.
		CHECK((first > 0) == (first > 0 && slots[first - 1].stub));
		int32_t shown = count_visible(slots, kCount, false);
		CHECK(shown >= 1 && shown < kCount);
		bool hiddenRight = first + shown < kCount;
		CHECK(!hiddenRight || slots[first + shown].stub);

		// Nothing is drawn outside the strip, nothing overlaps.
		float last = 0;
		for (int32_t i = 0; i < kCount; i++) {
			if (!slots[i].visible)
				continue;
			CHECK(slots[i].left >= last);
			CHECK(slots[i].right <= 400);
			last = slots[i].right;
		}
	}
}


static void
TestOverflowStubsReachEveryTab()
{
	// Clicking the right stub repeatedly must eventually activate the last
	// tab, and clicking the left stub must walk back to the first one.
	const int32_t kCount = 15;
	TabStripSlot slots[kCount];
	int32_t active = 0;
	int32_t first = 0;

	for (int32_t step = 0; step < kCount && active < kCount - 1; step++) {
		first = TabStripLayout::Layout(kMetrics, kCount, 0, 330, active,
			first, slots);
		int32_t stub = TabStripLayout::SlotAt(slots, kCount, 329);
		CHECK(stub >= 0);
		if (stub < 0)
			return;
		if (slots[stub].stub)
			active = stub;
		else
			active++;
	}
	first = TabStripLayout::Layout(kMetrics, kCount, 0, 330, active, first,
		slots);
	CHECK(active == kCount - 1);
	CHECK(slots[kCount - 1].visible && !slots[kCount - 1].stub);

	for (int32_t step = 0; step < kCount && active > 0; step++) {
		first = TabStripLayout::Layout(kMetrics, kCount, 0, 330, active,
			first, slots);
		int32_t stub = TabStripLayout::SlotAt(slots, kCount, 0);
		CHECK(stub >= 0);
		if (stub < 0)
			return;
		if (slots[stub].stub)
			active = stub;
		else
			active--;
	}
	CHECK(active == 0);
}


static void
TestFirstVisibleIsStable()
{
	// Moving the active tab inside the visible run must not scroll.
	const int32_t kCount = 12;
	TabStripSlot slots[kCount];
	int32_t first = TabStripLayout::Layout(kMetrics, kCount, 0, 400, 6, 0,
		slots);
	int32_t shown = count_visible(slots, kCount, false);
	for (int32_t active = first; active < first + shown; active++) {
		int32_t again = TabStripLayout::Layout(kMetrics, kCount, 0, 400,
			active, first, slots);
		CHECK(again == first);
	}
}


static void
TestDropIndex()
{
	TabStripSlot slots[4];
	TabStripLayout::Layout(kMetrics, 4, 0, 800, 1, 0, slots);
	float mid0 = (slots[0].left + slots[0].right) / 2;
	float mid2 = (slots[2].left + slots[2].right) / 2;
	float mid3 = (slots[3].left + slots[3].right) / 2;

	// Hovering on the own tab or just short of a neighbour's middle keeps
	// the position.
	CHECK(TabStripLayout::DropIndex(slots, 4, 1, slots[1].left + 5) == 1);
	CHECK(TabStripLayout::DropIndex(slots, 4, 1, mid2 - 1) == 1);
	CHECK(TabStripLayout::DropIndex(slots, 4, 1, mid0) == 1);
	// Crossing a neighbour's middle moves past it; far moves skip several.
	CHECK(TabStripLayout::DropIndex(slots, 4, 1, mid0 - 1) == 0);
	CHECK(TabStripLayout::DropIndex(slots, 4, 1, mid2) == 2);
	CHECK(TabStripLayout::DropIndex(slots, 4, 1, mid3 + 50) == 3);
	CHECK(TabStripLayout::DropIndex(slots, 4, 1, -100) == 0);
	// Bad input is left alone.
	CHECK(TabStripLayout::DropIndex(slots, 4, -1, 10) == -1);
	CHECK(TabStripLayout::DropIndex(slots, 4, 4, 10) == 4);
}


static void
TestDropIndexDoesNotEnterStubs()
{
	const int32_t kCount = 10;
	TabStripSlot slots[kCount];
	int32_t first = TabStripLayout::Layout(kMetrics, kCount, 0, 330, 5, 4,
		slots);
	CHECK(first > 0);
	CHECK(TabStripLayout::DropIndex(slots, kCount, first, -500) == first);
	int32_t shown = count_visible(slots, kCount, false);
	int32_t last = first + shown - 1;
	CHECK(TabStripLayout::DropIndex(slots, kCount, last, 5000) == last);
}


static void
TestDetachThreshold()
{
	// Strip from y=10 to y=38, threshold 20.
	CHECK(!TabStripLayout::ShouldDetach(10, 38, 24, 20));
	CHECK(!TabStripLayout::ShouldDetach(10, 38, -10, 20));
	CHECK(!TabStripLayout::ShouldDetach(10, 38, 58, 20));
	CHECK(TabStripLayout::ShouldDetach(10, 38, -10.5f, 20));
	CHECK(TabStripLayout::ShouldDetach(10, 38, 58.5f, 20));
}


static void
TestDegenerateInput()
{
	TabStripSlot slots[3];
	CHECK(TabStripLayout::Layout(kMetrics, 0, 0, 100, 0, 0, slots) == 0);
	CHECK(TabStripLayout::Layout(kMetrics, 3, 0, 100, 0, 0, NULL) == 0);

	// A strip narrower than one tab still shows the active tab, without
	// a close button, and never extends past the strip.
	int32_t first = TabStripLayout::Layout(kMetrics, 3, 0, 40, 2, 0, slots);
	CHECK(first == 2);
	CHECK(!slots[2].showsCloseButton);
	for (int32_t i = 0; i < 3; i++)
		CHECK(!slots[i].visible || slots[i].right <= 40);

	// Inverted strip bounds produce nothing visible but no crash.
	TabStripLayout::Layout(kMetrics, 3, 100, 0, 7, -4, slots);
	CHECK(TabStripLayout::SlotAt(slots, 3, 50) == -1);
}


int
main()
{
	TestSingleTabIsCapped();
	TestTabsShareTheStripWithoutGapsOrOverlap();
	TestFractionalStripEdges();
	TestNarrowInactiveTabsHideCloseButton();
	TestOverflowKeepsActiveTabVisible();
	TestOverflowStubsReachEveryTab();
	TestFirstVisibleIsStable();
	TestDropIndex();
	TestDropIndexDoesNotEnterStubs();
	TestDetachThreshold();
	TestDegenerateInput();

	if (sFailures != 0) {
		printf("%d check(s) failed\n", sFailures);
		return EXIT_FAILURE;
	}
	printf("test_tab_strip_layout: all checks passed\n");
	return EXIT_SUCCESS;
}
