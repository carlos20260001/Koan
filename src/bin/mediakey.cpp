/*
 * Copyright 2026, Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 */


/*!	mediakey: sends a keyboard's media key (play/pause, next, previous,
	stop) to the application that should act on it. The Shortcuts
	preferences bind the consumer-page key codes to it.

	The key goes to the active player of the media_controls_server (see
	<private/media_controls/MediaControls.h>) when there is one that takes
	it; else to an application that answers the older query protocol of
	<private/shared/MediaKeys.h>.
*/


#include <MediaControls.h>
#include <MediaKeys.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <Application.h>
#include <List.h>
#include <Messenger.h>
#include <OS.h>
#include <Roster.h>
#include <String.h>


static const bigtime_t kQueryTimeout = 300000;
static const bigtime_t kKeyTimeout = 2000000;

static bool sVerbose = false;


struct candidate {
	team_id	team;
	bool	playing;
	bool	active;
};


static void
usage(const char* program)
{
	fprintf(stderr, "usage: %s [-v] play-pause | play | pause | stop | next "
		"| previous\n"
		"       %s [-v] seek <seconds>\n"
		"       %s status\n"
		"Sends the key to the active player of the system media controls; "
		"without one,\nto the application that is playing, else to the "
		"active application, else to\nthe media application started last.\n"
		"status lists the players of the media controls, the active one "
		"first.\n"
		"  -v  say which applications answered and which one got the "
		"key\n", program, program, program);
	exit(1);
}


static int32
media_key_action(int32 key)
{
	switch (key) {
		case B_MEDIA_KEY_PLAY_PAUSE:
			return B_MEDIA_ACTION_TOGGLE;
		case B_MEDIA_KEY_PLAY:
			return B_MEDIA_ACTION_PLAY;
		case B_MEDIA_KEY_PAUSE:
			return B_MEDIA_ACTION_PAUSE;
		case B_MEDIA_KEY_STOP:
			return B_MEDIA_ACTION_STOP;
		case B_MEDIA_KEY_NEXT:
			return B_MEDIA_ACTION_NEXT;
		case B_MEDIA_KEY_PREVIOUS:
			return B_MEDIA_ACTION_PREVIOUS;
	}
	return -1;
}


static BString
format_time(bigtime_t time)
{
	BString text;
	int64 seconds = time / 1000000;
	text.SetToFormat("%" B_PRId64 ":%02" B_PRId64, seconds / 60, seconds % 60);
	return text;
}


static int
print_status()
{
	BMessage state;
	if (get_media_controls_state(&state) != B_OK) {
		printf("no players (the media controls server is not running)\n");
		return 0;
	}

	BMessage player;
	if (state.FindMessage("player", &player) != B_OK) {
		printf("no players\n");
		return 0;
	}

	for (int32 i = 0; state.FindMessage("player", i, &player) == B_OK; i++) {
		media_session_state playbackState = (media_session_state)
			player.GetInt32("state", B_MEDIA_SESSION_STOPPED);
		bigtime_t position = player.GetInt64("position", -1);
		if (position >= 0 && playbackState == B_MEDIA_SESSION_PLAYING) {
			position += (bigtime_t)((system_time()
				- player.GetInt64("position_time", system_time()))
				* player.GetFloat("rate", 1.0f));
		}

		printf("%s%s (%s, team %" B_PRId32 ", session %" B_PRIx64 "): %s",
			i == 0 ? "* " : "  ", player.GetString("name", "?"),
			player.GetString("signature", "?"), player.GetInt32("team", -1),
			player.GetInt64("session", -1),
			media_session_state_name(playbackState));
		if (position >= 0) {
			printf(" %s", format_time(position).String());
			bigtime_t duration = player.GetInt64("duration", 0);
			if (duration > 0)
				printf("/%s", format_time(duration).String());
		}
		printf("\n    title \"%s\", artist \"%s\", album \"%s\"%s\n",
			player.GetString("title", ""), player.GetString("artist", ""),
			player.GetString("album", ""),
			player.HasMessage("artwork") ? ", artwork" : "");

		uint32 actions = player.GetUInt32("actions", 0);
		printf("    actions:");
		for (int32 action = 0; action < B_MEDIA_ACTION_COUNT; action++) {
			if ((actions & B_MEDIA_ACTION_FLAG(action)) != 0)
				printf(" %s", media_session_action_name(action));
		}
		printf("\n");
	}
	return 0;
}


/*!	Gives the key to the system media controls' active player. Returns
	whether one took it.
*/
static bool
send_to_media_controls(int32 action, bigtime_t position = -1)
{
	bool handled = false;
	status_t status = send_media_controls_action(action, &handled, -1,
		position);
	if (sVerbose) {
		if (status == B_NAME_NOT_FOUND)
			printf("media controls: not running\n");
		else if (status != B_OK)
			printf("media controls: %s\n", strerror(status));
		else {
			printf("media controls: %s %s\n", media_session_action_name(action),
				handled ? "handled" : "not taken");
		}
	}
	return status == B_OK && handled;
}


static int32
parse_key(const char* name)
{
	for (int32 key = B_MEDIA_KEY_PLAY_PAUSE; key <= B_MEDIA_KEY_PREVIOUS;
			key++) {
		if (strcmp(name, media_key_name(key)) == 0)
			return key;
	}
	if (strcmp(name, "playpause") == 0 || strcmp(name, "toggle") == 0)
		return B_MEDIA_KEY_PLAY_PAUSE;
	if (strcmp(name, "prev") == 0)
		return B_MEDIA_KEY_PREVIOUS;
	return -1;
}


int
main(int argc, char** argv)
{
	int argi = 1;
	if (argi < argc && strcmp(argv[argi], "-v") == 0) {
		sVerbose = true;
		argi++;
	}
	if (argi == argc - 1 && strcmp(argv[argi], "status") == 0) {
		BApplication app("application/x-vnd.vos-mediakey");
		return print_status();
	}
	if (argi == argc - 2 && strcmp(argv[argi], "seek") == 0) {
		BApplication app("application/x-vnd.vos-mediakey");
		double seconds = atof(argv[argi + 1]);
		if (seconds < 0)
			usage(argv[0]);
		return send_to_media_controls(B_MEDIA_ACTION_SEEK,
			(bigtime_t)(seconds * 1000000)) ? 0 : 1;
	}
	if (argi != argc - 1)
		usage(argv[0]);

	int32 key = parse_key(argv[argi]);
	if (key < 0) {
		fprintf(stderr, "%s: unknown key \"%s\"\n", argv[0], argv[argi]);
		usage(argv[0]);
	}

	// Replies need a looper of our own.
	BApplication app("application/x-vnd.vos-mediakey");

	// Players registered with the media controls come first.
	if (send_to_media_controls(media_key_action(key)))
		return 0;

	app_info activeInfo;
	team_id activeTeam = -1;
	if (be_roster->GetActiveAppInfo(&activeInfo) == B_OK)
		activeTeam = activeInfo.team;

	BList teams;
	be_roster->GetAppList(&teams);

	BMessage query(kMsgMediaKey);
	query.AddInt32("key", key);
	query.AddBool("query", true);

	// The query goes to every application; most answer at once with
	// "not understood" or with no reply at all.
	candidate candidates[64];
	int32 candidateCount = 0;
	team_id ownTeam = app.Team();

	for (int32 i = 0; i < teams.CountItems(); i++) {
		team_id team = (team_id)(addr_t)teams.ItemAt(i);
		if (team == ownTeam)
			continue;

		BMessenger messenger(NULL, team);
		if (!messenger.IsValid())
			continue;

		BMessage reply;
		status_t status = messenger.SendMessage(&query, &reply, kQueryTimeout,
			kQueryTimeout);
		if (status != B_OK || reply.what != kMsgMediaKey)
			continue;

		candidate& found = candidates[candidateCount];
		found.team = team;
		found.playing = reply.GetBool("playing", false);
		found.active = team == activeTeam;
		if (sVerbose) {
			app_info info;
			be_roster->GetRunningAppInfo(team, &info);
			printf("%s (%" B_PRId32 "): can handle %s, playing %s%s\n",
				info.signature, team,
				reply.GetBool("can_handle", false) ? "yes" : "no",
				found.playing ? "yes" : "no", found.active ? ", active" : "");
		}
		if (!reply.GetBool("can_handle", false))
			continue;
		if (++candidateCount == (int32)B_COUNT_OF(candidates))
			break;
	}

	if (candidateCount == 0) {
		if (sVerbose)
			printf("no application takes %s\n", media_key_name(key));
		return 1;
	}

	// Choose: playing and active, else playing, else active, else the one
	// started last (the roster lists applications in the order they
	// registered).
	candidate* chosen = NULL;
	for (int32 pass = 0; pass < 3 && chosen == NULL; pass++) {
		for (int32 i = candidateCount - 1; i >= 0; i--) {
			candidate& found = candidates[i];
			bool matches = pass == 0 ? found.playing && found.active
				: pass == 1 ? found.playing : found.active;
			if (matches) {
				chosen = &found;
				break;
			}
		}
	}
	if (chosen == NULL)
		chosen = &candidates[candidateCount - 1];

	BMessage message(kMsgMediaKey);
	message.AddInt32("key", key);
	BMessage reply;
	status_t status = BMessenger(NULL, chosen->team).SendMessage(&message,
		&reply, kKeyTimeout, kKeyTimeout);
	bool handled = status == B_OK && reply.what == kMsgMediaKey
		&& reply.GetBool("handled", false);
	if (sVerbose) {
		app_info info;
		be_roster->GetRunningAppInfo(chosen->team, &info);
		printf("%s -> %s (%" B_PRId32 "): %s\n", media_key_name(key),
			info.signature, chosen->team, status != B_OK ? strerror(status)
				: handled ? "handled" : "not handled");
	}
	return handled ? 0 : 1;
}
