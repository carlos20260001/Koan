/*
 * Copyright 2026, Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 */


/*!	Tests the system media controls through their API: registers players
	with the media_controls_server (which the launch daemon starts), and
	checks which one is active and where actions go.

	Run it where no other player is registered (no MediaPlayer open).
	Exit status 0 when all checks pass.
*/


#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <Application.h>
#include <Bitmap.h>
#include <Handler.h>
#include <Locker.h>
#include <Looper.h>
#include <ObjectList.h>
#include <OS.h>
#include <Roster.h>

#include <MediaControls.h>
#include <MediaSession.h>


static int sFailures = 0;
static int sChecks = 0;


#define CHECK(condition, what...) \
	do { \
		sChecks++; \
		if (condition) { \
			printf("PASS: "); \
		} else { \
			printf("FAIL: "); \
			sFailures++; \
		} \
		printf(what); \
		printf("\n"); \
	} while (false)


class Receiver : public BHandler {
public:
	Receiver(const char* name)
		:
		BHandler(name),
		fSession(NULL)
	{
		fSemaphore = create_sem(0, name);
	}

	~Receiver()
	{
		delete_sem(fSemaphore);
	}

	void SetSession(MediaSession* session)
	{
		fSession = session;
	}

	virtual void MessageReceived(BMessage* message)
	{
		if (message->what != kMsgMediaSessionAction) {
			BHandler::MessageReceived(message);
			return;
		}

		BMessage* copy = new BMessage(*message);
		fLock.Lock();
		fMessages.AddItem(copy);
		fLock.Unlock();
		release_sem(fSemaphore);
	}

	//! The next action, or -1 after the timeout.
	int32 WaitAction(bigtime_t timeout = 2000000, bigtime_t* _position = NULL)
	{
		if (acquire_sem_etc(fSemaphore, 1, B_RELATIVE_TIMEOUT, timeout)
				!= B_OK)
			return -1;

		fLock.Lock();
		BMessage* message = fMessages.RemoveItemAt(0);
		fLock.Unlock();

		media_session_action action;
		bigtime_t position;
		int32 result = -2;
		if (fSession != NULL
			&& fSession->ParseAction(message, &action, &position) == B_OK) {
			result = action;
			if (_position != NULL)
				*_position = position;
		}
		delete message;
		return result;
	}

private:
	MediaSession*		fSession;
	sem_id				fSemaphore;
	BLocker				fLock;
	BObjectList<BMessage, true> fMessages;
};


//	#pragma mark - server state


static int32
count_players(const BMessage& state, team_id team = -1)
{
	BMessage player;
	int32 count = 0;
	for (int32 i = 0; state.FindMessage("player", i, &player) == B_OK; i++) {
		if (team < 0 || player.GetInt32("team", -1) == team)
			count++;
	}
	return count;
}


static bool
find_player(const BMessage& state, int64 session, BMessage* _player)
{
	for (int32 i = 0; state.FindMessage("player", i, _player) == B_OK; i++) {
		if (_player->GetInt64("session", -1) == session)
			return true;
	}
	return false;
}


static int64
active_session()
{
	BMessage state;
	if (get_media_controls_state(&state) != B_OK)
		return -1;
	return state.GetInt64("active", -1);
}


/*!	Waits until the server's state satisfies the condition (the updates
	are asynchronous).
*/
template<typename Condition>
static bool
wait_for_state(Condition condition, bigtime_t timeout = 5000000)
{
	bigtime_t end = system_time() + timeout;
	do {
		BMessage state;
		if (get_media_controls_state(&state) == B_OK && condition(state))
			return true;
		snooze(20000);
	} while (system_time() < end);
	return false;
}


static bool
wait_for_active(int64 session)
{
	return wait_for_state([session](const BMessage& state) {
		return state.GetInt64("active", -1) == session;
	});
}


static bool
wait_for_player_state(int64 session, media_session_state playbackState)
{
	return wait_for_state([session, playbackState](const BMessage& state) {
		BMessage player;
		return find_player(state, session, &player)
			&& player.GetInt32("state", -1) == playbackState;
	});
}


static bool
wait_for_actions(int64 session, uint32 actions)
{
	return wait_for_state([session, actions](const BMessage& state) {
		BMessage player;
		return find_player(state, session, &player)
			&& player.GetUInt32("actions", 0) == actions;
	});
}


static bool
send_action(int32 action, int64 session = -1, bigtime_t position = -1)
{
	bool handled = false;
	status_t status = send_media_controls_action(action, &handled, session,
		position);
	return status == B_OK && handled;
}


//	#pragma mark - child


static int
run_child()
{
	BApplication app("application/x-vnd.vos-media_session_test-child");
	BLooper* looper = new BLooper("child receiver");
	Receiver* receiver = new Receiver("child");
	looper->AddHandler(receiver);
	looper->Run();

	MediaSession session;
	session.SetMetadata("Child title", "Child artist");
	session.Register(receiver, "Child player");
	session.SetPlaybackState(B_MEDIA_SESSION_PLAYING, 0, 60000000);

	// The parent kills us: the server must notice without an unregister.
	snooze(30000000);
	return 0;
}


//	#pragma mark -


int
main(int argc, char** argv)
{
	if (argc > 1 && strcmp(argv[1], "child") == 0)
		return run_child();

	BApplication app("application/x-vnd.vos-media_session_test");
	team_id team = app.Team();

	BMessage state;
	if (get_media_controls_state(&state) == B_OK
		&& count_players(state) > 0) {
		printf("note: %" B_PRId32 " other player(s) registered, the active "
			"player checks may fail\n", count_players(state));
	}

	BLooper* looper = new BLooper("receivers");
	Receiver* receiverA = new Receiver("A");
	Receiver* receiverB = new Receiver("B");
	looper->AddHandler(receiverA);
	looper->AddHandler(receiverB);
	looper->Run();

	MediaSession* sessionA = new MediaSession;
	MediaSession* sessionB = new MediaSession;
	receiverA->SetSession(sessionA);
	receiverB->SetSession(sessionB);

	// 1. registration; nobody played yet: the last one registered
	sessionA->SetMetadata("Title A", "Artist A", "Album A");
	CHECK(sessionA->Register(receiverA, "Player A") == B_OK, "register A");
	CHECK(wait_for_state([team](const BMessage& state) {
			return count_players(state, team) == 1;
		}), "server sees A");
	CHECK(sessionB->Register(receiverB, "Player B") == B_OK, "register B");
	CHECK(wait_for_state([team](const BMessage& state) {
			return count_players(state, team) == 2;
		}), "server sees A and B");
	CHECK(active_session() == sessionB->ID(),
		"active = B (registered last, none played)");

	BMessage player;
	get_media_controls_state(&state);
	CHECK(find_player(state, sessionA->ID(), &player)
		&& strcmp(player.GetString("title", ""), "Title A") == 0
		&& strcmp(player.GetString("artist", ""), "Artist A") == 0
		&& strcmp(player.GetString("album", ""), "Album A") == 0
		&& strcmp(player.GetString("name", ""), "Player A") == 0,
		"A's metadata and name arrive with the registration");

	// 2. the one that started playing last is active
	sessionA->SetPlaybackState(B_MEDIA_SESSION_PLAYING, 0, 180000000);
	CHECK(wait_for_active(sessionA->ID()), "A plays -> active = A");
	sessionB->SetPlaybackState(B_MEDIA_SESSION_PLAYING, 0, 120000000);
	CHECK(wait_for_active(sessionB->ID()), "B plays too -> active = B");
	sessionB->SetPlaybackState(B_MEDIA_SESSION_PAUSED, 5000000, 120000000);
	CHECK(wait_for_player_state(sessionB->ID(), B_MEDIA_SESSION_PAUSED)
		&& active_session() == sessionA->ID(),
		"B pauses, A still plays -> active = A");
	sessionA->SetPlaybackState(B_MEDIA_SESSION_PAUSED, 9000000, 180000000);
	CHECK(wait_for_player_state(sessionA->ID(), B_MEDIA_SESSION_PAUSED)
		&& active_session() == sessionA->ID(),
		"A pauses after B -> active = A (played last)");

	// 3. actions go to the active player
	CHECK(send_action(B_MEDIA_ACTION_TOGGLE), "toggle is handled");
	CHECK(receiverA->WaitAction() == B_MEDIA_ACTION_TOGGLE,
		"A gets the toggle");
	CHECK(receiverB->WaitAction(200000) == -1, "B gets nothing");

	// 4. a player without toggle gets play or pause instead
	uint32 playPause = B_MEDIA_ACTION_FLAG(B_MEDIA_ACTION_PLAY)
		| B_MEDIA_ACTION_FLAG(B_MEDIA_ACTION_PAUSE);
	sessionB->SetSupportedActions(playPause);
	CHECK(wait_for_actions(sessionB->ID(), playPause),
		"B takes play and pause only");
	CHECK(send_action(B_MEDIA_ACTION_TOGGLE, sessionB->ID()),
		"toggle for B is handled");
	CHECK(receiverB->WaitAction() == B_MEDIA_ACTION_PLAY,
		"paused B gets play for toggle");

	// 5. actions a player does not take are not handled
	CHECK(!send_action(B_MEDIA_ACTION_NEXT),
		"next is not handled: A does not take it");
	CHECK(receiverA->WaitAction(200000) == -1, "A gets no next");

	uint32 actionsA = B_MEDIA_DEFAULT_ACTIONS
		| B_MEDIA_ACTION_FLAG(B_MEDIA_ACTION_NEXT)
		| B_MEDIA_ACTION_FLAG(B_MEDIA_ACTION_PREVIOUS)
		| B_MEDIA_ACTION_FLAG(B_MEDIA_ACTION_SEEK);
	sessionA->SetSupportedActions(actionsA);
	CHECK(wait_for_actions(sessionA->ID(), actionsA),
		"A takes next, previous and seek now");
	CHECK(send_action(B_MEDIA_ACTION_NEXT) &&
		receiverA->WaitAction() == B_MEDIA_ACTION_NEXT, "A gets next");
	bigtime_t position = -1;
	CHECK(send_action(B_MEDIA_ACTION_SEEK, -1, 42000000)
		&& receiverA->WaitAction(2000000, &position) == B_MEDIA_ACTION_SEEK
		&& position == 42000000, "A gets seek to 42 s");

	// 6. the user's choice wins until another player starts playing
	BMessage activate(kMsgMediaControlsActivate);
	activate.AddInt64("session", sessionB->ID());
	BMessenger(kMediaControlsServerSignature).SendMessage(&activate);
	CHECK(wait_for_active(sessionB->ID()), "activate B -> active = B");
	sessionA->SetPlaybackState(B_MEDIA_SESSION_PLAYING, 9000000, 180000000);
	CHECK(wait_for_active(sessionA->ID()), "A starts playing -> active = A");

	// 7. artwork is scaled down to 256 x 256
	BBitmap artwork(BRect(0, 0, 511, 383), B_RGBA32);
	memset(artwork.Bits(), 0x80, artwork.BitsLength());
	sessionA->SetMetadata("Title A2", "Artist A2", NULL, &artwork);
	CHECK(wait_for_state([sessionA](const BMessage& state) {
			BMessage player;
			return find_player(state, sessionA->ID(), &player)
				&& strcmp(player.GetString("title", ""), "Title A2") == 0;
		}), "A's new metadata arrives");
	get_media_controls_state(&state);
	BMessage artworkArchive;
	BBitmap* received = NULL;
	if (find_player(state, sessionA->ID(), &player)
		&& player.FindMessage("artwork", &artworkArchive) == B_OK)
		received = new BBitmap(&artworkArchive);
	CHECK(received != NULL && received->IsValid()
		&& received->Bounds().Width() + 1 == 256
		&& received->Bounds().Height() + 1 == 192
		&& !player.HasString("album"),
		"artwork arrives as 256 x 192, the album is gone");
	delete received;

	// 8. unregister
	sessionA->Unregister();
	CHECK(wait_for_state([team](const BMessage& state) {
			return count_players(state, team) == 1;
		}) && active_session() == sessionB->ID(),
		"A unregisters -> active = B");

	// 9. a player whose team dies goes away
	const char* childArgs[] = { argv[0], "child", NULL };
	thread_id child = load_image(2, childArgs, (const char**)environ);
	CHECK(child >= 0, "start the child player");
	if (child >= 0) {
		resume_thread(child);
		CHECK(wait_for_state([child](const BMessage& state) {
				BMessage player;
				return state.FindMessage("player", &player) == B_OK
					&& player.GetInt32("team", -1) == child
					&& player.GetInt32("state", -1)
						== B_MEDIA_SESSION_PLAYING;
			}), "the playing child is active");
		kill_team(child);
		status_t returnValue;
		wait_for_thread(child, &returnValue);
		CHECK(wait_for_state([child](const BMessage& state) {
				return count_players(state, child) == 0;
			}) && active_session() == sessionB->ID(),
			"the child's team died -> its player is gone, active = B");
	}

	// 10. the last one goes
	delete sessionB;
	CHECK(wait_for_state([team](const BMessage& state) {
			return count_players(state, team) == 0;
		}), "deleting B unregisters it");

	delete sessionA;
	looper->Lock();
	looper->Quit();

	printf("%d of %d checks passed\n", sChecks - sFailures, sChecks);
	return sFailures == 0 ? 0 : 1;
}
