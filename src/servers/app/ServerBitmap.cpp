/*
 * Copyright 2001-2010, Haiku.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 *		DarkWyrm <bpmagic@columbus.rr.com>
 *		Axel Dörfler, axeld@pinc-software.de
 */


#include "ServerBitmap.h"

#include <new>
#include <cmath>
#include <math.h>
#include <Bitmap.h>
#include <BitmapPrivate.h>
#include <IconUtils.h>
#include <Message.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "BitmapManager.h"
#include "ClientMemoryAllocator.h"
#include "ColorConversion.h"
#include "HWInterface.h"
#include "InterfacePrivate.h"
#include "Overlay.h"
#include "ServerApp.h"
#include "SystemPalette.h"


using std::nothrow;
using namespace BPrivate;


struct BitmapVectorIcon {
	BitmapVectorIcon() : pixels(NULL), scale(0) {}
	~BitmapVectorIcon() { delete[] pixels; }
	BMessage description;
	BRect bounds;
	BRect crop;
	uint8* pixels;
	float scale;
	BReference<ServerBitmap> raster;
};


/*!	A word about memory housekeeping and why it's implemented this way:

	The reason why this looks so complicated is to optimize the most common
	path (bitmap creation from the application), and don't cause any further
	memory allocations for maintaining memory in that case.
	If a bitmap was allocated this way, both, the fAllocator and
	fAllocationCookie members are used.

	For overlays, the allocator only allocates a small piece of client memory
	for use with the overlay_client_data structure - the actual buffer will be
	placed in the graphics frame buffer and is allocated by the graphics driver.

	If the memory was allocated on the app_server heap, neither fAllocator, nor
	fAllocationCookie are used, and the buffer is just freed in that case when
	the bitmap is destructed. This method is mainly used for cursors.
*/


/*!	\brief Constructor called by the BitmapManager (only).
	\param rect Size of the bitmap.
	\param space Color space of the bitmap
	\param flags Various bitmap flags to tweak the bitmap as defined in Bitmap.h
	\param bytesperline Number of bytes in each row. -1 implies the default
		value. Any value less than the the default will less than the default
		will be overridden, but any value greater than the default will result
		in the number of bytes specified.
	\param screen Screen assigned to the bitmap.
*/
ServerBitmap::ServerBitmap(BRect rect, color_space space, uint32 flags,
		int32 bytesPerRow, screen_id screen)
	:
	fMemory(NULL),
	fOverlay(NULL),
	fBuffer(NULL),
	// WARNING: '1' is added to the width and height.
	// Same is done in FBBitmap subclass, so if you
	// modify here make sure to do the same under
	// FBBitmap::SetSize(...)
	fWidth(rect.IntegerWidth() + 1),
	fHeight(rect.IntegerHeight() + 1),
	fBytesPerRow(0),
	fSpace(space),
	fFlags(flags),
	fOwner(NULL),
	fVectorIcon(NULL)
	// fToken is initialized (if used) by the BitmapManager
{
	mutex_init(&fIconLock, "bitmap icon");
	int32 minBytesPerRow = get_bytes_per_row(space, fWidth);

	fBytesPerRow = max_c(bytesPerRow, minBytesPerRow);
}


//! Copy constructor does not copy the buffer.
ServerBitmap::ServerBitmap(const ServerBitmap* bitmap)
	:
	fMemory(NULL),
	fOverlay(NULL),
	fBuffer(NULL),
	fOwner(NULL),
	fVectorIcon(NULL)
{
	mutex_init(&fIconLock, "bitmap icon");
	if (bitmap) {
		fWidth = bitmap->fWidth;
		fHeight = bitmap->fHeight;
		fBytesPerRow = bitmap->fBytesPerRow;
		fSpace = bitmap->fSpace;
		fFlags = bitmap->fFlags;
	} else {
		fWidth = 0;
		fHeight = 0;
		fBytesPerRow = 0;
		fSpace = B_NO_COLOR_SPACE;
		fFlags = 0;
	}
}


ServerBitmap::~ServerBitmap()
{
	delete fVectorIcon;
	mutex_destroy(&fIconLock);
	if (fMemory != NULL) {
		if (fMemory != &fClientMemory)
			delete fMemory;
	} else
		delete[] fBuffer;
}


status_t
ServerBitmap::SetVectorIcon(const BMessage& description)
{
	const void* data;
	ssize_t size;
	BRect bounds, crop;
	if (!IsValid() || BitsLength() > 4 * 1024 * 1024
		|| (Flags() & (B_BITMAP_ACCEPTS_VIEWS | B_BITMAP_WILL_OVERLAY)) != 0
		|| description.FindData("data", B_RAW_TYPE, &data, &size) != B_OK
		|| size <= 0 || size > 256 * 1024
		|| description.FindRect("bounds", &bounds) != B_OK
		|| description.FindRect("crop", &crop) != B_OK
		|| !bounds.IsValid() || !bounds.Contains(crop)
		|| bounds.left != 0 || bounds.top != 0
		|| bounds.right > 1023 || bounds.bottom > 1023
		|| !std::isfinite(crop.left) || !std::isfinite(crop.top)
		|| !std::isfinite(crop.right) || !std::isfinite(crop.bottom)
		|| crop.Width() + 1 != Width() || crop.Height() + 1 != Height())
		return B_BAD_VALUE;
	int32 effect;
	for (int32 i = 0; description.FindInt32("effect", i, &effect) == B_OK; i++) {
		if (i >= 8 || effect < B_VECTOR_ICON_UNCHANGED
			|| effect > B_VECTOR_ICON_CMAP8)
			return B_BAD_VALUE;
	}

	BitmapVectorIcon* icon = new(std::nothrow) BitmapVectorIcon;
	if (icon == NULL)
		return B_NO_MEMORY;
	icon->pixels = new(std::nothrow) uint8[BitsLength()];
	if (icon->pixels == NULL) {
		delete icon;
		return B_NO_MEMORY;
	}
	memcpy(icon->pixels, Bits(), BitsLength());
	icon->description = description;
	icon->bounds = bounds;
	icon->crop = crop;
	MutexLocker lock(fIconLock);
	delete fVectorIcon;
	fVectorIcon = icon;
	return B_OK;
}


BReference<ServerBitmap>
ServerBitmap::ScaledIcon(float scale, BRect& source) const
{
	BReference<ServerBitmap> result;
	if (scale <= 1 || !std::isfinite(scale))
		return result;
	MutexLocker lock(fIconLock);
	BitmapVectorIcon* icon = fVectorIcon;
	if (icon == NULL)
		return result;
	if (memcmp(icon->pixels, Bits(), BitsLength()) != 0) {
		delete fVectorIcon;
		fVectorIcon = NULL;
		return result;
	}

	// Round to a whole raster width, retaining the aspect ratio. Cropping is
	// done by BitmapPainter, so trimmed button icons retain their geometry.
	float width = ceilf((icon->bounds.Width() + 1) * scale);
	if (width > 4096)
		return result;
	scale = width / (icon->bounds.Width() + 1);
	float height = ceilf((icon->bounds.Height() + 1) * scale);
	if (height > 4096)
		return result;
	if (!icon->raster.IsSet() || icon->scale != scale) {
		BBitmap raster(BRect(0, 0, width - 1, height - 1),
			B_BITMAP_NO_SERVER_LINK, B_RGBA32);
		const void* data;
		ssize_t size;
		if (raster.InitCheck() != B_OK
			|| icon->description.FindData("data", B_RAW_TYPE, &data, &size) != B_OK
			|| BIconUtils::GetVectorIcon((const uint8*)data, size, &raster) != B_OK)
			return result;

		int32 effect;
		for (int32 i = 0; icon->description.FindInt32("effect", i, &effect)
				== B_OK; i++) {
			for (int32 y = 0; y < height; y++) {
				uint8* p = (uint8*)raster.Bits() + y * raster.BytesPerRow();
				for (int32 x = 0; x < width; x++, p += 4) {
					if (effect == B_VECTOR_ICON_CMAP8) {
						const color_map* map = SystemColorMap();
						uint16 index = ((p[2] & 0xf8) << 7) | ((p[1] & 0xf8) << 2)
							| (p[0] >> 3);
						uint8 colorIndex = p[3] < 128 ? B_TRANSPARENT_MAGIC_CMAP8
							: map->index_map[index];
						rgb_color color = map->color_list[colorIndex];
						p[0] = color.blue;
						p[1] = color.green;
						p[2] = color.red;
						p[3] = colorIndex == B_TRANSPARENT_MAGIC_CMAP8 ? 0 : 255;
					} else if (effect == B_VECTOR_ICON_OPAQUE)
						p[3] = 255;
					if (effect == B_VECTOR_ICON_DISABLED
						|| effect == B_VECTOR_ICON_DISABLED_ACTIVE) {
						uint8 grey = (p[0] * 10 + p[1] * 60 + p[2] * 30) / 100;
						for (int c = 0; c < 3; c++)
							p[c] = (uint8)(grey + (p[c] - grey) * 0.3f);
						p[3] = (uint8)(p[3] * 0.3f);
					} else if (effect == B_VECTOR_ICON_DISABLED_OPAQUE
						|| effect == B_VECTOR_ICON_DISABLED_ACTIVE_OPAQUE) {
						uint8 grey = effect == B_VECTOR_ICON_DISABLED_OPAQUE ? 216 : 188;
						for (int c = 0; c < 3; c++)
							p[c] = (uint8)(grey + (p[c] - grey) * 0.4f);
						p[3] = 255;
					}
					if (effect == B_VECTOR_ICON_ACTIVE
						|| effect == B_VECTOR_ICON_DISABLED_ACTIVE) {
						for (int c = 0; c < 3; c++)
							p[c] = (uint8)(p[c] * 0.8f);
					} else if (effect == B_VECTOR_ICON_SELECTED) {
						for (int c = 0; c < 3; c++)
							p[c] = (p[c] * 168) >> 8;
					}
				}
			}
		}
		BReference<ServerBitmap> scaled(new(std::nothrow) UtilityBitmap(
			raster.Bounds(), B_RGBA32, 0), true);
		if (!scaled.IsSet() || !scaled->IsValid()
			|| scaled->ImportBits(raster.Bits(), raster.BitsLength(),
				raster.BytesPerRow(), B_RGBA32) != B_OK)
			return result;
		icon->raster = scaled;
		icon->scale = scale;
	}
	source.OffsetBy(icon->crop.LeftTop());
	source.Set(source.left * scale, source.top * scale,
		(source.right + 1) * scale - 1, (source.bottom + 1) * scale - 1);
	return icon->raster;
}


/*!	\brief Internal function used by subclasses

	Subclasses should call this so the buffer can automagically
	be allocated on the heap.
*/
void
ServerBitmap::AllocateBuffer()
{
	uint32 length = BitsLength();
	if (length > 0) {
		delete[] fBuffer;
		fBuffer = new(std::nothrow) uint8[length];
	}
}


status_t
ServerBitmap::ImportBits(const void *bits, int32 bitsLength, int32 bytesPerRow,
	color_space colorSpace)
{
	if (!bits || bitsLength < 0 || bytesPerRow <= 0)
		return B_BAD_VALUE;

	return BPrivate::ConvertBits(bits, fBuffer, bitsLength, BitsLength(),
		bytesPerRow, fBytesPerRow, colorSpace, fSpace, fWidth, fHeight);
}


status_t
ServerBitmap::ImportBits(const void *bits, int32 bitsLength, int32 bytesPerRow,
	color_space colorSpace, BPoint from, BPoint to, int32 width, int32 height)
{
	if (!bits || bitsLength < 0 || bytesPerRow <= 0 || width < 0 || height < 0)
		return B_BAD_VALUE;

	return BPrivate::ConvertBits(bits, fBuffer, bitsLength, BitsLength(),
		bytesPerRow, fBytesPerRow, colorSpace, fSpace, from, to, width,
		height);
}


area_id
ServerBitmap::Area() const
{
	if (fMemory != NULL)
		return fMemory->Area();

	return B_ERROR;
}


size_t
ServerBitmap::AreaOffset() const
{
	if (fMemory != NULL)
		return fMemory->AreaOffset();

	return 0;
}


void
ServerBitmap::SetOverlay(::Overlay* overlay)
{
	fOverlay.SetTo(overlay);
}


::Overlay*
ServerBitmap::Overlay() const
{
	return fOverlay.Get();
}


void
ServerBitmap::SetOwner(ServerApp* owner)
{
	fOwner = owner;
}


ServerApp*
ServerBitmap::Owner() const
{
	return fOwner;
}


void
ServerBitmap::PrintToStream()
{
	printf("Bitmap@%p: (%" B_PRId32 ":%" B_PRId32 "), space %" B_PRId32 ", "
		"bpr %" B_PRId32 ", buffer %p\n", this, fWidth, fHeight, (int32)fSpace,
		fBytesPerRow, fBuffer);
}


//	#pragma mark -


UtilityBitmap::UtilityBitmap(BRect rect, color_space space, uint32 flags,
		int32 bytesPerRow, screen_id screen)
	:
	ServerBitmap(rect, space, flags, bytesPerRow, screen)
{
	AllocateBuffer();
}


UtilityBitmap::UtilityBitmap(const ServerBitmap* bitmap)
	:
	ServerBitmap(bitmap)
{
	AllocateBuffer();

	if (bitmap->Bits())
		memcpy(Bits(), bitmap->Bits(), bitmap->BitsLength());
}


UtilityBitmap::UtilityBitmap(const uint8* alreadyPaddedData, uint32 width,
		uint32 height, color_space format)
	:
	ServerBitmap(BRect(0, 0, width - 1, height - 1), format, 0)
{
	AllocateBuffer();
	if (Bits())
		memcpy(Bits(), alreadyPaddedData, BitsLength());
}


UtilityBitmap::~UtilityBitmap()
{
}
