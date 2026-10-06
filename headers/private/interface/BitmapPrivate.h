/*
 * Copyright 2006, Haiku, Inc.
 * Distributed under the terms of the MIT License.
 */
#ifndef _BITMAP_PRIVATE_H
#define _BITMAP_PRIVATE_H


#include <Bitmap.h>
#include <OS.h>


// This structure is placed in the client/server shared memory area.

struct overlay_client_data {
	sem_id	lock;
	uint8*	buffer;
};


void reconnect_bitmaps_to_app_server();


// Effects used by BIcon and Tracker when deriving another vector icon bitmap.
enum {
	B_VECTOR_ICON_UNCHANGED = 0,
	B_VECTOR_ICON_ACTIVE,
	B_VECTOR_ICON_DISABLED,
	B_VECTOR_ICON_DISABLED_ACTIVE,
	B_VECTOR_ICON_SELECTED,
	B_VECTOR_ICON_DISABLED_OPAQUE,
	B_VECTOR_ICON_DISABLED_ACTIVE_OPAQUE,
	B_VECTOR_ICON_OPAQUE,
	B_VECTOR_ICON_CMAP8
};


class BBitmap::Private {
public:
								Private(BBitmap* bitmap);
			void				ReconnectToAppServer();
			int32				ServerToken() const;
			status_t			SetVectorIcon(const uint8* data, size_t size);
			status_t			SetVectorIcon(const BMessage& description);
			status_t			GetVectorIcon(BMessage& description) const;
			void				CopyVectorIcon(const BBitmap* source,
									uint32 effect = B_VECTOR_ICON_UNCHANGED,
									const BRect* crop = NULL);
private:
			BBitmap*			fBitmap;
};

#endif // _BITMAP_PRIVATE_H
