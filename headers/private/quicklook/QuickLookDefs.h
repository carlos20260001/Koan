/*
 * Copyright 2026, air/OS contributors.
 * Distributed under the terms of the MIT License.
 */
#ifndef _QUICK_LOOK_DEFS_H
#define _QUICK_LOOK_DEFS_H


/*!	The protocol between a client (Tracker) and the quick_look server. */


#define B_QUICK_LOOK_SIGNATURE "application/x-vnd.vos-quick_look_server"


enum {
	/*!	Show the panel for the entries in "refs" (entry_ref), starting with
		"index" (int32, 0). "frame" (BRect) is the client's window frame, so
		the panel appears on that screen; "icon" (BRect) is where the entry
		is shown on screen, which the panel zooms out of and back into;
		"owner" (BMessenger) is told when the panel is closed. Sent again
		with new refs while the panel is up, it shows those instead. With
		"update" (bool) true it only replaces what a panel already up for
		this owner shows; a hidden panel stays hidden and the owner is
		told kMsgQuickLookClosed. */
	kMsgQuickLookShow			= 'QLsh',
	//! Hide the panel.
	kMsgQuickLookHide			= 'QLhd',
	//! Show, or hide when it is up for this owner.
	kMsgQuickLookToggle			= 'QLtg',

	//! Server to owner: the panel is up for its entries.
	kMsgQuickLookShown			= 'QLsn',
	//! Server to owner: the panel was closed (close button, Open, replaced).
	kMsgQuickLookClosed			= 'QLcl',

	/*!	A thumbnail for "ref" (entry_ref) at most "size" (int32) pixels on
		a side, made by the add-ons on a thread of their own. The reply has
		"bitmap" (an archived BBitmap) or "error" (int32): B_NOT_SUPPORTED
		when nothing makes thumbnails of that type. */
	kMsgQuickLookThumbnail		= 'QLtn'
};


#endif	// _QUICK_LOOK_DEFS_H
