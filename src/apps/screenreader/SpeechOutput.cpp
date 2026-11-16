/*
 * Copyright 2026, air/OS contributors.
 * Distributed under the terms of the MIT License.
 */


#include "SpeechOutput.h"

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <Autolock.h>
#include <Message.h>
#include <LaunchRoster.h>
#include <Path.h>
#include <Roster.h>

#include <AccessibilityProtocol.h>


extern char** environ;


SpeechOutput::SpeechOutput(const char* engine, int32 rate)
	:
	fEngine(kEngineNone),
	fFlite(false),
	fRate(rate),
	fLock("speech queue"),
	fSem(-1),
	fThread(-1),
	fCurrent(-1),
	fQuit(false)
{
	_Detect(engine);

	if (fEngine == kEngineCommand) {
		fSem = create_sem(0, "speech queue");
		fThread = spawn_thread(_ThreadEntry, "speech", B_NORMAL_PRIORITY,
			this);
		if (fThread >= 0)
			resume_thread(fThread);
	}
}


SpeechOutput::~SpeechOutput()
{
	if (fThread >= 0) {
		{
			BAutolock locker(fLock);
			fQuit = true;
			fQueue.MakeEmpty();
			if (fCurrent >= 0)
				kill_team(fCurrent);
		}
		release_sem(fSem);
		status_t status;
		wait_for_thread(fThread, &status);
	}
	if (fSem >= 0)
		delete_sem(fSem);
}


void
SpeechOutput::Speak(const char* text, bool interrupt)
{
	if (text == NULL || text[0] == '\0')
		return;

	switch (fEngine) {
		case kEngineServer:
		{
			BMessage message(B_SPEECH_SPEAK);
			message.AddString("text", text);
			message.AddBool("interrupt", interrupt);
			if (fRate > 0)
				message.AddInt32("rate", fRate);
			message.AddString("client", "ScreenReader");
			fServer.SendMessage(&message, (BHandler*)NULL, 100000);
			break;
		}

		case kEngineCommand:
		{
			BAutolock locker(fLock);
			if (interrupt) {
				fQueue.MakeEmpty();
				if (fCurrent >= 0)
					kill_team(fCurrent);
			}
			fQueue.Add(text);
			release_sem(fSem);
			break;
		}

		default:
			break;
	}
}


void
SpeechOutput::Stop()
{
	if (fEngine == kEngineServer) {
		BMessage message(B_SPEECH_STOP);
		fServer.SendMessage(&message, (BHandler*)NULL, 100000);
	} else if (fEngine == kEngineCommand) {
		BAutolock locker(fLock);
		fQueue.MakeEmpty();
		if (fCurrent >= 0)
			kill_team(fCurrent);
	}
}


void
SpeechOutput::WaitUntilDone(bigtime_t timeout)
{
	if (fEngine != kEngineCommand)
		return;

	bigtime_t end = system_time() + timeout;
	while (system_time() < end) {
		{
			BAutolock locker(fLock);
			if (fQueue.IsEmpty() && fCurrent < 0)
				return;
		}
		snooze(20000);
	}
}


void
SpeechOutput::_Detect(const char* engine)
{
	BString wanted(engine);
	if (wanted == "none") {
		fEngineName = "none";
		return;
	}

	if (wanted.IsEmpty() || wanted == "auto" || wanted == "speech_server") {
		// Ask the launch_daemon only for a service: BMessenger(signature)
		// waits forever for a plain job that is not launched.
		BMessenger server;
		app_info info;
		BMessage job;
		if (be_roster->GetAppInfo(B_SPEECH_SERVER_SIGNATURE, &info) == B_OK)
			server = BMessenger(NULL, info.team);
		else if (BLaunchRoster().GetJobInfo("x-vnd.vos-speech_server", job)
				== B_OK && job.GetBool("service", false)
				&& job.GetBool("enabled", false)) {
			server = BMessenger(B_SPEECH_SERVER_SIGNATURE);
				// launch_daemon starts the on-demand service
		}
		if (server.IsValid()) {
			fServer = server;
			fEngine = kEngineServer;
			fEngineName = "speech_server";
			return;
		}
	}

	const char* kCommands[] = { "speak", "espeak-ng", "espeak", "flite",
		NULL };
	for (int32 i = 0; kCommands[i] != NULL; i++) {
		if (!wanted.IsEmpty() && wanted != "auto" && wanted != kCommands[i])
			continue;
		BString path;
		if (_FindCommand(kCommands[i], path)) {
			fCommand = path;
			fFlite = strcmp(kCommands[i], "flite") == 0;
			fEngine = kEngineCommand;
			fEngineName = kCommands[i];
			return;
		}
	}

	if (!wanted.IsEmpty() && wanted != "auto" && _FindCommand(wanted, fCommand)) {
		// any other command that takes the text as its last argument
		fEngine = kEngineCommand;
		fEngineName = wanted;
		return;
	}

	fEngineName = "none";
}


bool
SpeechOutput::_FindCommand(const char* name, BString& path)
{
	if (name[0] == '/') {
		if (access(name, X_OK) != 0)
			return false;
		path = name;
		return true;
	}

	BString searchPath(getenv("PATH"));
	searchPath << ":/boot/system/bin:/boot/home/config/non-packaged/bin"
		":/boot/system/non-packaged/bin";
	BStringList directories;
	searchPath.Split(":", true, directories);
	for (int32 i = 0; i < directories.CountStrings(); i++) {
		BString candidate = directories.StringAt(i);
		candidate << "/" << name;
		if (access(candidate.String(), X_OK) == 0) {
			path = candidate;
			return true;
		}
	}
	return false;
}


team_id
SpeechOutput::_Launch(const BString& text)
{
	BString speakText(text);
	if (speakText.StartsWith("-"))
		speakText.Prepend(" ");

	BString rate;
	rate << fRate;

	const char* argv[6];
	int argc = 0;
	argv[argc++] = fCommand.String();
	if (fFlite) {
		argv[argc++] = "-t";
	} else if (fRate > 0) {
		argv[argc++] = "-s";
		argv[argc++] = rate.String();
	}
	argv[argc++] = speakText.String();
	argv[argc] = NULL;

	thread_id team = load_image(argc, argv, (const char**)environ);
	if (team < 0)
		return team;
	resume_thread(team);
	return team;
}


/*static*/ status_t
SpeechOutput::_ThreadEntry(void* data)
{
	((SpeechOutput*)data)->_Thread();
	return B_OK;
}


void
SpeechOutput::_Thread()
{
	while (acquire_sem(fSem) == B_OK) {
		BString text;
		team_id team;
		{
			BAutolock locker(fLock);
			if (fQuit)
				break;
			if (fQueue.IsEmpty())
				continue;
			text = fQueue.StringAt(0);
			fQueue.Remove(0);
			fCurrent = team = _Launch(text);
			if (team < 0) {
				fCurrent = -1;
				continue;
			}
		}

		status_t status;
		wait_for_thread(team, &status);

		BAutolock locker(fLock);
		fCurrent = -1;
		if (fQuit)
			break;
	}
}
