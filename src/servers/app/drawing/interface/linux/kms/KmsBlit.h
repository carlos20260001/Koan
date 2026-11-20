/*
 * Copyright 2026, Koan contributors. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef KMS_BLIT_H
#define KMS_BLIT_H


/*	The CPU path: copies from the frame buffer app_server draws into to a
	scan-out buffer, and the resample of a region to its monitor's mode
	when the render scale is not the monitor's (no display engine or GPU
	scaling). 32 bit pixels throughout (XRGB8888 / premultiplied ARGB8888,
	B_RGB32 / B_RGBA32 in memory).
*/


#include "KmsLayout.h"


// copies \a area of the source to the same place of the destination
void	kms_copy_rect(const uint8* source, uint32 sourceBytesPerRow,
			uint8* destination, uint32 destinationBytesPerRow,
			const KmsRect& area, int32 destinationX, int32 destinationY,
			bool opaque);

void	kms_fill_rect(uint8* destination, uint32 bytesPerRow,
			const KmsRect& area, uint32 color);


/*!	Filters a source of one size to a destination of another: a tent
	filter of radius max(1, source / destination) source pixels in each
	direction (bilinear when enlarging, an area average when shrinking),
	weights summing to 1 << 14, edges clamped to the source. The same
	sizes copy exactly. Only the destination rectangle asked for is
	computed, so that a damaged part costs what it covers.
*/
class KmsScaler {
public:
								KmsScaler();
								~KmsScaler();

			status_t			SetTo(int32 sourceWidth, int32 sourceHeight,
									int32 destinationWidth,
									int32 destinationHeight);
			bool				IsIdentity() const;

			// source points at the region's first pixel; destination at
			// the destination's (0, 0); area is in destination pixels
			void				Scale(const uint8* source,
									uint32 sourceBytesPerRow,
									uint8* destination,
									uint32 destinationBytesPerRow,
									const KmsRect& area, bool opaque) const;

			int32				SourceWidth() const { return fSourceWidth; }
			int32				SourceHeight() const { return fSourceHeight; }
			int32				DestinationWidth() const
									{ return fDestinationWidth; }
			int32				DestinationHeight() const
									{ return fDestinationHeight; }

private:
			struct Axis {
				int32*			first;		// per destination pixel
				int32*			count;
				int32*			weights;	// count per pixel, packed
				int32*			offsets;	// into weights
				int32			maxTaps;
			};

	static	status_t			_BuildAxis(Axis& axis, int32 sourceLength,
									int32 destinationLength);
	static	void				_FreeAxis(Axis& axis);

			int32				fSourceWidth;
			int32				fSourceHeight;
			int32				fDestinationWidth;
			int32				fDestinationHeight;
			Axis				fHorizontal;
			Axis				fVertical;
};


#endif	// KMS_BLIT_H
