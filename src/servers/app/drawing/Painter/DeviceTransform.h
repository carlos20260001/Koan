/*
 * Copyright 2026, Koan contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef DEVICE_TRANSFORM_H
#define DEVICE_TRANSFORM_H

#include <SupportDefs.h>

#include <agg_trans_affine.h>


namespace DeviceTransform {

/*!	Builds the transform Painter applies to logical (BeAPI) drawing
	coordinates: the view's user transform, applied around the view's
	screen offset, followed by the screen scale (logical -> framebuffer
	pixels).

	AGG composes left to right: "a *= b" applies a first, then b.
*/
inline agg::trans_affine
Compose(const agg::trans_affine& user, bool userIsIdentity, int32 offsetX,
	int32 offsetY, float scale)
{
	agg::trans_affine transform;
	if (!userIsIdentity) {
		transform = agg::trans_affine_translation(-offsetX, -offsetY);
		transform *= user;
		transform *= agg::trans_affine_translation(offsetX, offsetY);
	}

	// The screen scale is the outermost step: p -> scale * U(p). Scaling
	// first would leave the user translation and the view offset unscaled.
	if (scale != 1.0f)
		transform *= agg::trans_affine_scaling(scale);

	return transform;
}

}	// namespace DeviceTransform

#endif	// DEVICE_TRANSFORM_H
