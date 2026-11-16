/*
 * Copyright 2026, air/OS contributors.
 * Distributed under the terms of the MIT License.
 */
#ifndef _ACCESSIBILITY_PROTOCOL_H
#define _ACCESSIBILITY_PROTOCOL_H


/*!	Messages between applications, the accessibility_server and assistive
	technologies (screen reader, test tools). See
	docs/airos/ACCESSIBILITY.md for the full description.
*/


#define B_ACCESSIBILITY_SERVER_SIGNATURE \
	"application/x-vnd.vos-accessibility_server"
#define B_SCREEN_READER_SIGNATURE "application/x-vnd.vos-ScreenReader"

// Proposed speech service of haiku#52 (text to speech). The screen reader
// uses it when it runs, and falls back to an engine command, then captions.
#define B_SPEECH_SERVER_SIGNATURE "application/x-vnd.vos-speech_server"


enum {
	// server -> application (sent to the BApplication)
	B_ACCESSIBILITY_ENABLE			= 'acEN',
		// "server" messenger
	B_ACCESSIBILITY_DISABLE			= 'acDS',

	// application -> server
	B_ACCESSIBILITY_REGISTER		= 'acRG',
		// "bridge" messenger, "team" int32, "signature" string
	B_ACCESSIBILITY_EVENT			= 'acEV',
		// "event" int32 (accessible_event), "node" message, plus per event:
		// "window" message (focus, window events), "window_active" bool,
		// "container" message (list item events), "text" string, "offset"
		// int32, "length" int32 (text events), "caret" int32, "char" string,
		// "line" int32, "line_text" string (caret events), "interrupt" bool
		// (announcements). The server adds "team" int32, "signature" string
		// and "bridge" messenger before passing it on to its clients.

	// assistive technology -> server
	B_ACCESSIBILITY_SUBSCRIBE		= 'acSB',
		// "client" messenger; the reply lists the registered applications
		// like B_ACCESSIBILITY_GET_APPS
	B_ACCESSIBILITY_UNSUBSCRIBE		= 'acUS',
		// "client" messenger
	B_ACCESSIBILITY_GET_APPS		= 'acGA',
		// reply: "app" messages with "team", "signature", "bridge"

	// assistive technology -> application bridge (synchronous)
	B_ACCESSIBILITY_GET_ROOT		= 'acGR',
		// "depth" int32 (optional); reply: "node" (role APPLICATION)
	B_ACCESSIBILITY_GET_NODE		= 'acGN',
		// "id" int64, "depth" int32 (0: no children, -1: whole subtree);
		// reply: "node"
	B_ACCESSIBILITY_GET_FOCUS		= 'acGF',
		// reply: "window" and "node" (the focus, if any)
	B_ACCESSIBILITY_PERFORM_ACTION	= 'acPA',
		// "id" int64, "action" int32 (accessible_action)
	B_ACCESSIBILITY_GET_TEXT		= 'acGT',
		// "id" int64, "start" int32, "end" int32; reply: "text"

	B_ACCESSIBILITY_REPLY			= 'acRP',
		// "status" int32 (status_t), plus the request's results

	// review commands for the screen reader (sent by the accessibility_keys
	// input filter, or by anything else)
	B_SCREEN_READER_READ_FOCUS		= 'srRF',
	B_SCREEN_READER_READ_WINDOW		= 'srRW',
	B_SCREEN_READER_READ_ALL		= 'srRA',
	B_SCREEN_READER_STOP			= 'srST',
	B_SCREEN_READER_BROWSE			= 'srBR',
		// browse mode: "direction" int32 (1 next, -1 previous), "kind"
		// int32 (0 any item, 1 heading, 2 link, 3 form field, 4 list,
		// 5 landmark)
	B_SCREEN_READER_ACTIVATE		= 'srAC',
		// press (or focus, for a field) the browse mode item

	// proposed speech_server protocol (haiku#52)
	B_SPEECH_SPEAK					= 'SPsp',
		// "text" string, "interrupt" bool, "rate" int32 (words per minute,
		// optional), "client" string
	B_SPEECH_STOP					= 'SPst',
};


// node message fields
#define B_ACCESSIBLE_FIELD_ID			"id"			// int64
#define B_ACCESSIBLE_FIELD_PARENT		"parent"		// int64, 0 = none
#define B_ACCESSIBLE_FIELD_ROLE			"role"			// int32
#define B_ACCESSIBLE_FIELD_ROLE_NAME	"role_name"		// string (English)
#define B_ACCESSIBLE_FIELD_NAME			"name"			// string
#define B_ACCESSIBLE_FIELD_DESCRIPTION	"description"	// string
#define B_ACCESSIBLE_FIELD_VALUE		"value"			// string
#define B_ACCESSIBLE_FIELD_STATES		"states"		// int64 (bits)
#define B_ACCESSIBLE_FIELD_FRAME		"frame"			// BRect, screen
#define B_ACCESSIBLE_FIELD_ACTIONS		"actions"		// int32 (bits)
#define B_ACCESSIBLE_FIELD_CHILD_COUNT	"child_count"	// int32
#define B_ACCESSIBLE_FIELD_CHILD_IDS	"child_ids"		// int64[]
#define B_ACCESSIBLE_FIELD_CHILD		"child"			// message[]
#define B_ACCESSIBLE_FIELD_CURRENT		"current"		// double
#define B_ACCESSIBLE_FIELD_MINIMUM		"minimum"		// double
#define B_ACCESSIBLE_FIELD_MAXIMUM		"maximum"		// double
#define B_ACCESSIBLE_FIELD_SEL_START	"sel_start"		// int32
#define B_ACCESSIBLE_FIELD_SEL_END		"sel_end"		// int32
#define B_ACCESSIBLE_FIELD_INDEX		"index"			// int32, 0-based
#define B_ACCESSIBLE_FIELD_COUNT		"count"			// int32 (siblings)
#define B_ACCESSIBLE_FIELD_LEVEL		"level"			// int32 (trees)
#define B_ACCESSIBLE_FIELD_SHORTCUT		"shortcut"		// string


#endif	// _ACCESSIBILITY_PROTOCOL_H
