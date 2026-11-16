/*
 * Copyright 2026, Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef _ON_SCREEN_DISPLAY_H
#define _ON_SCREEN_DISPLAY_H


#include <Message.h>
#include <Messenger.h>


// The osd_server shows a level, like the volume or the screen brightness,
// for a moment after a key changed it. The launch daemon starts the server
// when the first message arrives.

#define kOSDServerSignature "application/x-vnd.vos-osd_server"


enum {
	kMsgShowLevel = 'osdL'
		// "kind"	int32, one of osd_level_kind
		// "level"	float, from 0 to 1: how much of the meter is lit
		// "muted"	bool, optional
		// "gain"	float, optional: the volume in dB, shown as its value;
		//			without it, the level is shown in percent
};


enum osd_level_kind {
	B_OSD_VOLUME = 0,
	B_OSD_BRIGHTNESS
};


static inline status_t
show_osd_level(BMessage& message)
{
	// A short timeout: the volume and brightness tools must not wait for
	// a server that is not there.
	return BMessenger(kOSDServerSignature).SendMessage(&message,
		(BHandler*)NULL, 100000);
}


#endif	// _ON_SCREEN_DISPLAY_H
