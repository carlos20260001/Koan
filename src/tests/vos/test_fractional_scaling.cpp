/*
 * Copyright 2026, Koan contributors
 * SPDX-License-Identifier: Apache-2.0
 */

/*!	Deterministic render/hit-test consistency checks for fractional screen
	scaling (docs/analysis/fractional-scaling.md, findings F1-F3), run at
	1.0, 1.25, 1.5 and 1.75:

	- F1: the device transform is "user transform, then screen scale";
	- F2: logical rects map to a physical partition (no shared or missing
	  pixel between neighbours), and every physical pixel is owned by the
	  logical rect pointer hit-testing assigns it to;
	- F3: a window-move/scroll blit is only done when the copied pixels are
	  exactly what a repaint at the new position would draw.

	Hit-testing is modelled as app_server does it: MouseFilter converts the
	physical pointer with Desktop::PhysicalToLogical (ScreenScaleTransform::
	ToLogical) and BRegion::Contains(BPoint) truncates to int.
*/

#include <math.h>
#include <stdio.h>

#include "ScreenScaleTransform.h"
#include "drawing/Painter/DeviceTransform.h"


static int sFailures = 0;
static const float kScales[] = { 1.0f, 1.25f, 1.5f, 1.75f };
static const BRect kPhysicalFrame(0, 0, 1919, 1079);


static void
check(bool condition, const char* name, float scale)
{
	if (!condition) {
		printf("FAIL|%s-%.2f\n", name, scale);
		sFailures++;
	} else
		printf("PASS|%s-%.2f\n", name, scale);
}


static bool
near(double a, double b)
{
	return fabs(a - b) < 1e-3;
}


//! Logical column app_server hit-tests a physical pointer x into.
static int32
hit_test_column(int32 physicalX, float scale)
{
	BRect logicalFrame
		= ScreenScaleTransform::ToLogicalFrame(kPhysicalFrame, scale);
	BPoint logical = ScreenScaleTransform::ToLogical(BPoint(physicalX, 0),
		kPhysicalFrame, logicalFrame, scale);
	return (int32)logical.x;
}


static BRect
partition(float left, float right, float scale)
{
	return ScreenScaleTransform::ToPhysicalPartitionRect(
		BRect(left, 0, right, 0), scale);
}


// #pragma mark - F1


static void
test_transform_order(float scale)
{
	// View translated by (10, 0): logical (0,0) must land at 10 * scale.
	agg::trans_affine translate = agg::trans_affine_translation(10, 0);
	agg::trans_affine device
		= DeviceTransform::Compose(translate, false, 0, 0, scale);
	double x = 0, y = 0;
	device.transform(&x, &y);
	check(near(x, 10.0 * scale) && near(y, 0), "transform-translate", scale);

	// View scaled by 2 around its screen origin (100, 20).
	agg::trans_affine zoom = agg::trans_affine_scaling(2.0);
	device = DeviceTransform::Compose(zoom, false, 100, 20, scale);
	const double points[][2] = { { 0, 0 }, { 150, 7 }, { 100, 20 } };
	bool ok = true;
	for (uint32 i = 0; i < sizeof(points) / sizeof(points[0]); i++) {
		double px = points[i][0], py = points[i][1];
		double ex = scale * (2 * (px - 100) + 100);
		double ey = scale * (2 * (py - 20) + 20);
		device.transform(&px, &py);
		ok = ok && near(px, ex) && near(py, ey);
	}
	check(ok, "transform-scale-around-origin", scale);

	// Identity user transform is the plain screen scale.
	device = DeviceTransform::Compose(agg::trans_affine(), true, 100, 20,
		scale);
	x = 33, y = 44;
	device.transform(&x, &y);
	check(near(x, 33.0 * scale) && near(y, 44.0 * scale),
		"transform-identity", scale);
}


// #pragma mark - F2


static void
test_partition(float scale)
{
	const int32 kColumns = 40;

	// Single logical pixels tile the physical row without overlap or gap.
	bool tiles = partition(0, 0, scale).left == 0;
	for (int32 c = 0; c < kColumns; c++) {
		BRect a = partition(c, c, scale);
		BRect b = partition(c + 1, c + 1, scale);
		tiles = tiles && a.IsValid() && a.right + 1 == b.left;
	}
	check(tiles, "partition-columns-tile", scale);

	// Two adjacent windows share no physical column.
	BRect left = partition(0, 9, scale);
	BRect right = partition(10, 19, scale);
	check(left.right + 1 == right.left, "partition-adjacent-windows", scale);

	// Hit-test owner == painted owner for every physical pixel.
	int32 end = (int32)partition(kColumns - 1, kColumns - 1, scale).right;
	bool coherent = true;
	for (int32 px = 0; px <= end; px++) {
		int32 owners = 0;
		int32 owner = -1;
		for (int32 c = 0; c < kColumns; c++) {
			BRect r = partition(c, c, scale);
			if (px >= r.left && px <= r.right) {
				owners++;
				owner = c;
			}
		}
		if (owners != 1 || owner != hit_test_column(px, scale)) {
			printf("  px %d: %d owner(s), painted by %d, hit-tests to %d\n",
				(int)px, (int)owners, (int)owner,
				(int)hit_test_column(px, scale));
			coherent = false;
		}
	}
	check(coherent, "partition-matches-hit-test", scale);

	// Partition of a wide rect equals the union of its columns.
	BRect wide = partition(3, 17, scale);
	check(wide.left == partition(3, 3, scale).left
			&& wide.right == partition(17, 17, scale).right,
		"partition-union-of-columns", scale);
}


// #pragma mark - F3


static void
test_move_blit(float scale)
{
	const BRect window(3, 5, 40, 30);
	BRect source = ScreenScaleTransform::ToPhysicalPartitionRect(window,
		scale);

	bool blitMatchesRepaint = true;
	bool keepsExactBlits = true;
	for (int32 d = -8; d <= 8; d++) {
		if (d == 0)
			continue;

		int32 physical;
		bool blit = ScreenScaleTransform::ToPhysicalOffset(d, scale,
			physical);

		double exact = d * (double)scale;
		if (exact == floor(exact) && !blit)
			keepsExactBlits = false;
		if (!blit)
			continue;

		// Repaint: content point q is drawn at (q + d) * scale and the
		// window owns the partition of its moved frame.
		BRect repainted = ScreenScaleTransform::ToPhysicalPartitionRect(
			window.OffsetByCopy(d, d), scale);
		BRect blitted = source.OffsetByCopy(physical, physical);
		if (repainted != blitted || !near(exact, physical)) {
			printf("  offset %d: blit by %d px, repaint moves by %.2f px\n",
				(int)d, (int)physical, exact);
			blitMatchesRepaint = false;
		}
	}
	check(blitMatchesRepaint, "move-blit-matches-repaint", scale);
	check(keepsExactBlits, "move-blit-kept-when-exact", scale);
}


// #pragma mark - F8: pointer-grab confinement


static void
test_confine_rect(float scale)
{
	// A client confines the pointer to its window's logical frame; the
	// device clamps in physical pixels. Every physical pixel the device
	// lets the pointer reach must hit-test inside the logical rect, and
	// every logical column/row of the rect must be reachable.
	BRect logicalFrame
		= ScreenScaleTransform::ToLogicalFrame(kPhysicalFrame, scale);
	const BRect confine(100, 50, 299, 199);
	BRect device = ScreenScaleTransform::ToDeviceConfineRect(confine,
		kPhysicalFrame, logicalFrame, scale);

	bool inside = device.IsValid();
	bool reachable[2][400] = {};
	for (int32 px = (int32)device.left; inside && px <= device.right; px++) {
		int32 column = hit_test_column(px, scale);
		inside = column >= confine.left && column <= confine.right;
		if (inside)
			reachable[0][column] = true;
	}
	for (int32 py = (int32)device.top; inside && py <= device.bottom; py++) {
		BPoint logical = ScreenScaleTransform::ToLogical(BPoint(0, py),
			kPhysicalFrame, logicalFrame, scale);
		int32 row = (int32)logical.y;
		inside = row >= confine.top && row <= confine.bottom;
		if (inside)
			reachable[1][row] = true;
	}
	if (!inside) {
		printf("  device rect %.0f,%.0f-%.0f,%.0f leaves logical %.0f,%.0f-"
			"%.0f,%.0f\n", device.left, device.top, device.right,
			device.bottom, confine.left, confine.top, confine.right,
			confine.bottom);
	}
	check(inside, "confine-device-rect-inside", scale);

	bool all = true;
	for (int32 c = (int32)confine.left; c <= confine.right; c++)
		all = all && reachable[0][c];
	for (int32 r = (int32)confine.top; r <= confine.bottom; r++)
		all = all && reachable[1][r];
	check(inside && all, "confine-device-rect-covers", scale);
}


int
main()
{
	for (uint32 i = 0; i < sizeof(kScales) / sizeof(kScales[0]); i++) {
		test_transform_order(kScales[i]);
		test_partition(kScales[i]);
		test_move_blit(kScales[i]);
		test_confine_rect(kScales[i]);
	}

	printf("SUMMARY|fail=%d\n", sFailures);
	return sFailures == 0 ? 0 : 1;
}
