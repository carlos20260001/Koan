/*
 * Copyright 2026, Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef _MEDIA_CONTROLS_H
#define _MEDIA_CONTROLS_H


/*!	The system media controls ("now playing"): applications that play media
	register a player with the media_controls_server and tell it what they
	play (title, artist, album, artwork), in which state (playing, paused,
	stopped, position and duration) and what they can do (play, pause, next,
	...). The server keeps one of the players "active" and sends it the
	actions that come from the keyboard's media keys (through the mediakey
	command that the Shortcuts preferences bind to them) and from its Deskbar
	replicant.

	This is the message protocol; it is what stays stable. Applications
	normally use the BPrivate::MediaSession class (<MediaSession.h>,
	libmedia_controls.a) instead of these messages. docs/airos/MEDIA-CONTROLS.md
	has the whole picture.

	The launch daemon starts the server when the first message arrives.
*/


#include <Message.h>
#include <Messenger.h>
#include <OS.h>
#include <Roster.h>


#define kMediaControlsServerSignature \
	"application/x-vnd.vos-media_controls_server"

// The name of the server's Deskbar replicant.
#define kMediaControlsReplicantName "media_controls"


enum media_session_action {
	B_MEDIA_ACTION_PLAY = 0,
	B_MEDIA_ACTION_PAUSE,
	B_MEDIA_ACTION_TOGGLE,		// play when paused or stopped, else pause
	B_MEDIA_ACTION_STOP,
	B_MEDIA_ACTION_NEXT,
	B_MEDIA_ACTION_PREVIOUS,
	B_MEDIA_ACTION_SEEK,		// "position" int64: where to go, in µs

	B_MEDIA_ACTION_COUNT
};

#define B_MEDIA_ACTION_FLAG(action)	(1UL << (action))

// The actions a player takes when it does not say otherwise.
#define B_MEDIA_DEFAULT_ACTIONS \
	(B_MEDIA_ACTION_FLAG(B_MEDIA_ACTION_PLAY) \
		| B_MEDIA_ACTION_FLAG(B_MEDIA_ACTION_PAUSE) \
		| B_MEDIA_ACTION_FLAG(B_MEDIA_ACTION_TOGGLE) \
		| B_MEDIA_ACTION_FLAG(B_MEDIA_ACTION_STOP))


enum media_session_state {
	B_MEDIA_SESSION_STOPPED = 0,
	B_MEDIA_SESSION_PLAYING,
	B_MEDIA_SESSION_PAUSED
};


enum {
	// application -> server (no replies)

	kMsgMediaSessionRegister	= 'mcRg',
		// "session"	int64, chosen by the application: unique per team
		//				(the class uses team << 32 | counter)
		// "target"		messenger, gets the kMsgMediaSessionAction messages
		// "name"		string, optional: what to call the player (else the
		//				application's name)
		// plus any of the fields of kMsgMediaSessionUpdate
	kMsgMediaSessionUpdate		= 'mcUp',
		// "session"	int64
		// A group of fields replaces all of that group, fields of other
		// groups stay as they are:
		// metadata:	"title" string (its presence marks the group),
		//				"artist", "album" string, "artwork" message (an
		//				archived BBitmap, at most 256 x 256)
		// playback:	"state" int32 (media_session_state) marks the group,
		//				"position" int64 (µs), "position_time" int64 (the
		//				system_time() of the position), "duration" int64
		//				(µs, 0 or missing: unknown or live), "rate" float
		//				(1 = normal speed)
		// actions:		"actions" uint32 (B_MEDIA_ACTION_FLAG()s)
	kMsgMediaSessionUnregister	= 'mcUr',
		// "session"	int64

	// server -> application

	kMsgMediaSessionAction		= 'mcAc',
		// "session"	int64
		// "action"		int32 (media_session_action)
		// "position"	int64 (µs), for B_MEDIA_ACTION_SEEK

	// anyone -> server

	kMsgMediaControlsSendAction	= 'mcSA',
		// "action"		int32 (media_session_action)
		// "session"	int64, optional: the player; else the active one
		// "position"	int64, for B_MEDIA_ACTION_SEEK
		// reply: kMsgMediaControlsReply with "handled" bool (true when a
		// player that takes the action got it) and "session" int64
	kMsgMediaControlsGetState	= 'mcGS',
		// reply: kMsgMediaControlsState
	kMsgMediaControlsStartWatching = 'mcSW',
		// "target"		messenger: gets a kMsgMediaControlsState now and
		//				after every change
	kMsgMediaControlsStopWatching = 'mcSw',
		// "target"		messenger
	kMsgMediaControlsActivate	= 'mcAt',
		// "session"	int64: make this player the active one

	// server -> anyone

	kMsgMediaControlsState		= 'mcSt',
		// "active"		int64, the active player (missing: no player)
		// "player"		message, for each player, the active one first:
		//				"session", "name", "signature", "team" int32, and
		//				the fields of the three groups above
	kMsgMediaControlsReply		= 'mcRp'
};


static inline const char*
media_session_action_name(int32 action)
{
	switch (action) {
		case B_MEDIA_ACTION_PLAY:
			return "play";
		case B_MEDIA_ACTION_PAUSE:
			return "pause";
		case B_MEDIA_ACTION_TOGGLE:
			return "toggle";
		case B_MEDIA_ACTION_STOP:
			return "stop";
		case B_MEDIA_ACTION_NEXT:
			return "next";
		case B_MEDIA_ACTION_PREVIOUS:
			return "previous";
		case B_MEDIA_ACTION_SEEK:
			return "seek";
	}
	return "?";
}


static inline const char*
media_session_state_name(int32 state)
{
	switch (state) {
		case B_MEDIA_SESSION_STOPPED:
			return "stopped";
		case B_MEDIA_SESSION_PLAYING:
			return "playing";
		case B_MEDIA_SESSION_PAUSED:
			return "paused";
	}
	return "?";
}


/*!	Sends an action to a player through the server (\a session < 0: the
	active one) and says whether one took it. It does not start the server
	when it is not running: then there is no player.
*/
static inline status_t
send_media_controls_action(int32 action, bool* _handled = NULL,
	int64 session = -1, bigtime_t position = -1,
	bigtime_t timeout = 500000)
{
	if (_handled != NULL)
		*_handled = false;

	// (The launch daemon would start the server for a message.)
	if (!be_roster->IsRunning(kMediaControlsServerSignature))
		return B_NAME_NOT_FOUND;
	BMessenger server(kMediaControlsServerSignature);

	BMessage request(kMsgMediaControlsSendAction);
	request.AddInt32("action", action);
	if (session >= 0)
		request.AddInt64("session", session);
	if (position >= 0)
		request.AddInt64("position", position);

	BMessage reply;
	status_t status = server.SendMessage(&request, &reply, timeout, timeout);
	if (status != B_OK)
		return status;
	if (_handled != NULL)
		*_handled = reply.GetBool("handled", false);
	return B_OK;
}


//!	Gets the server's kMsgMediaControlsState (the players), if it runs.
static inline status_t
get_media_controls_state(BMessage* state, bigtime_t timeout = 500000)
{
	if (!be_roster->IsRunning(kMediaControlsServerSignature))
		return B_NAME_NOT_FOUND;
	BMessenger server(kMediaControlsServerSignature);

	BMessage request(kMsgMediaControlsGetState);
	return server.SendMessage(&request, state, timeout, timeout);
}


#endif	// _MEDIA_CONTROLS_H
