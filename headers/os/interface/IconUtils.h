/*
 * Copyright 2006-2023, Haiku. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef _ICON_UTILS_H
#define _ICON_UTILS_H


#include <GraphicsDefs.h>
#include <Mime.h>
#include <Size.h>

class BBitmap;
class BNode;


class BIconUtils {
								BIconUtils();
								~BIconUtils();
								BIconUtils(const BIconUtils&);
			BIconUtils&			operator=(const BIconUtils&);

public:
	static	status_t			GetIcon(BNode* node,
									const char* vectorIconAttrName,
									const char* smallIconAttrName,
									const char* largeIconAttrName,
									icon_size size, BBitmap* result);

	static	status_t			GetVectorIcon(BNode* node,
									const char* attrName, BBitmap* result);

	static	status_t			GetVectorIcon(const uint8* buffer,
									size_t size, BBitmap* result);

	static	status_t			GetCMAP8Icon(BNode* node,
									const char* smallIconAttrName,
									const char* largeIconAttrName,
									icon_size size, BBitmap* icon);

	static	status_t			GetSystemIcon(const char* iconName, BBitmap* result);

	// Screen-scale aware icons. IconScale() is the main screen's scale
	// (1 without an app_server connection). CreateIconBitmap() returns a
	// bitmap of logicalSize times that scale, with its density set, so it
	// draws at logicalSize; fill it with GetVectorIcon() or GetIcon().
	static	float				IconScale();
	static	BBitmap*			CreateIconBitmap(BSize logicalSize,
									uint32 flags = 0,
									color_space colorSpace = B_RGBA32);

	static	status_t			ConvertFromCMAP8(BBitmap* source,
									BBitmap* result);
	static	status_t			ConvertToCMAP8(BBitmap* source,
									BBitmap* result);

	static	status_t			ConvertFromCMAP8(const uint8* data,
									uint32 width, uint32 height,
									uint32 bytesPerRow, BBitmap* result);

	static	status_t			ConvertToCMAP8(const uint8* data,
									uint32 width, uint32 height,
									uint32 bytesPerRow, BBitmap* result);
};

#endif	// _ICON_UTILS_H
