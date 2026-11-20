/*
 * Copyright 2026, air/OS. All rights reserved.
 * Copyright 2005-2009, Ingo Weinhold, bonefish@users.sf.net.
 * Distributed under the terms of the MIT License.
 */

/*!	debug_server on Linux: air/OS's crash dialog for crashes that
	systemd-coredump recorded.

	On Haiku the debug_server is the kernel's default debugger: a crashing
	team stops, and the server asks the user whether to kill it, debug it,
	save a report or write a core file (DebugServer.cpp). Linux has no such
	hook for a user program; the kernel hands the core dump to
	systemd-coredump, which logs it to the journal. This server follows the
	journal for the coredumps of its own user's programs and shows the same
	dialog (DebugWindow) once the program is gone:
	- "Terminate": nothing more (the program has ended);
	- "Save report": `coredumpctl info` into a report on the Desktop;
	- "Write core file": `coredumpctl dump` into a core file on the Desktop;
	- "Debug": `coredumpctl debug` (gdb) in a Terminal.
	The actions follow the same settings as on air/OS
	(~/config/settings/system/debug_server/settings: default_action,
	executable_actions).
*/


#include "DebugWindow.h"

#include <errno.h>
#include <fcntl.h>
#include <spawn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include <systemd/sd-journal.h>

#include <DriverSettings.h>
#include <FindDirectory.h>
#include <Path.h>
#include <RegExp.h>
#include <Roster.h>
#include <Server.h>
#include <String.h>


extern char** environ;


static const char* kSignature = "application/x-vnd.vos-debug_server";
static const char* kTerminalSignature = "application/x-vnd.vos-Terminal";

// systemd-coredump's journal message ID
static const char* kCoredumpMatch
	= "MESSAGE_ID=fc2e22bc6ee647b6b90729ab34a250b1";


// #pragma mark - settings (as in DebugServer.cpp)


static status_t
action_for_string(const char* action, int32& _action)
{
	if (strcmp(action, "kill") == 0)
		_action = kActionKillTeam;
	else if (strcmp(action, "debug") == 0)
		_action = kActionDebugTeam;
	else if (strcmp(action, "log") == 0
		|| strcmp(action, "report") == 0) {
		_action = kActionSaveReportTeam;
	} else if (strcasecmp(action, "core") == 0)
		_action = kActionWriteCoreFile;
	else if (strcasecmp(action, "user") == 0)
		_action = kActionPromptUser;
	else
		return B_BAD_VALUE;

	return B_OK;
}


static bool
match_team_name(const char* teamName, const char* parameterName)
{
	RegExp expressionMatcher;
	if (expressionMatcher.SetPattern(parameterName,
		RegExp::PATTERN_TYPE_WILDCARD)) {
		BString value = teamName;
		if (parameterName[0] != '/') {
			BPath path(teamName);
			if (path.InitCheck() == B_OK)
				value = path.Leaf();
		}

		RegExp::MatchResult match = expressionMatcher.Match(value);
		if (match.HasMatched())
			return true;
	}

	return false;
}


static int32
action_for_team(const char* teamName)
{
	int32 action = kActionPromptUser;

	BPath path;
	if (find_directory(B_USER_SETTINGS_DIRECTORY, &path) != B_OK)
		return action;
	path.Append("system/debug_server/settings");
	BDriverSettings settings;
	if (settings.Load(path.Path()) != B_OK)
		return action;

	int32 tempAction;
	if (action_for_string(settings.GetParameterValue("default_action",
			"user", "user"), tempAction) == B_OK)
		action = tempAction;

	BDriverParameter parameter = settings.GetParameter("executable_actions");
	for (BDriverParameterIterator iterator = parameter.ParameterIterator();
			iterator.HasNext();) {
		BDriverParameter child = iterator.Next();
		if (!match_team_name(teamName, child.Name()))
			continue;
		if (child.CountValues() > 0
			&& action_for_string(child.ValueAt(0), tempAction) == B_OK)
			action = tempAction;
		break;
	}

	return action;
}


// #pragma mark - actions


/*!	Runs coredumpctl \a verb for \a pid with its output in \a outputPath
	(or with "-o outputPath" for "dump"); waits for it. */
static status_t
run_coredumpctl(const char* verb, const char* pid, const char* outputPath)
{
	posix_spawn_file_actions_t actions;
	posix_spawn_file_actions_init(&actions);
	posix_spawn_file_actions_addopen(&actions, STDIN_FILENO, "/dev/null",
		O_RDONLY, 0);

	bool dump = strcmp(verb, "dump") == 0;
	if (!dump) {
		posix_spawn_file_actions_addopen(&actions, STDOUT_FILENO, outputPath,
			O_WRONLY | O_CREAT | O_TRUNC, 0644);
	}

	const char* args[] = { "coredumpctl", "--no-pager", verb, pid,
		dump ? "-o" : NULL, dump ? outputPath : NULL, NULL };
	pid_t child;
	int status = posix_spawnp(&child, "coredumpctl", &actions, NULL,
		(char* const*)args, environ);
	posix_spawn_file_actions_destroy(&actions);
	if (status != 0)
		return B_ERROR;

	int exitStatus;
	if (waitpid(child, &exitStatus, 0) < 0)
		return B_ERROR;
	return WIFEXITED(exitStatus) && WEXITSTATUS(exitStatus) == 0
		? B_OK : B_ERROR;
}


static BString
desktop_file(const char* name, const char* pid, const char* suffix)
{
	BPath path;
	if (find_directory(B_DESKTOP_DIRECTORY, &path) != B_OK)
		find_directory(B_USER_DIRECTORY, &path);

	char date[32];
	time_t now = time(NULL);
	struct tm tm;
	strftime(date, sizeof(date), "%Y-%m-%d-%H%M%S",
		localtime_r(&now, &tm));

	BString file;
	file.SetToFormat("%s/%s-%s-%s%s", path.Path(), name, pid, date, suffix);
	return file;
}


static void
handle_crash(const char* executable, const char* name, const char* pid)
{
	int32 action = action_for_team(executable);
	if (action == kActionPromptUser) {
		if (be_app->Lock()) {
			status_t status = ((BServer*)be_app)->InitGUIContext();
			be_app->Unlock();
			if (status != B_OK)
				return;
		}
		DebugWindow* window = new DebugWindow(name);
		action = window->Go();
		if (action < 0)
			action = kActionKillTeam;
	}

	switch (action) {
		case kActionSaveReportTeam:
		{
			BString report = desktop_file(name, pid, ".report");
			status_t status = run_coredumpctl("info", pid, report.String());
			printf("debug_server: report for %s (%s) in %s: %s\n", name, pid,
				report.String(), strerror(status));
			break;
		}
		case kActionWriteCoreFile:
		{
			BString core = desktop_file(name, pid, ".core");
			status_t status = run_coredumpctl("dump", pid, core.String());
			printf("debug_server: core file of %s (%s) in %s: %s\n", name,
				pid, core.String(), strerror(status));
			break;
		}
		case kActionDebugTeam:
		{
			const char* args[] = { "coredumpctl", "debug", pid, NULL };
			status_t status = be_roster->Launch(kTerminalSignature, 3,
				args);
			if (status != B_OK) {
				printf("debug_server: cannot open a Terminal to debug %s: "
					"%s\n", name, strerror(status));
			}
			break;
		}
		case kActionKillTeam:
		default:
			// the program has ended already
			break;
	}
}


// #pragma mark - server


class CoredumpServer : public BServer {
public:
								CoredumpServer(status_t& error);

			status_t			Init();
	virtual	bool				QuitRequested();

private:
	static	status_t			_ListenerEntry(void* data);
			void				_Listener();

			thread_id			fListener;
};


CoredumpServer::CoredumpServer(status_t& error)
	:
	BServer(kSignature, false, &error),
	fListener(-1)
{
}


status_t
CoredumpServer::Init()
{
	fListener = spawn_thread(_ListenerEntry, "coredump listener",
		B_NORMAL_PRIORITY, this);
	if (fListener < 0)
		return fListener;
	return resume_thread(fListener);
}


bool
CoredumpServer::QuitRequested()
{
	// Never give up, never surrender. ;-)
	return false;
}


/*static*/ status_t
CoredumpServer::_ListenerEntry(void* data)
{
	((CoredumpServer*)data)->_Listener();
	return B_OK;
}


static bool
journal_field(sd_journal* journal, const char* name, BString& value)
{
	const void* data;
	size_t length;
	if (sd_journal_get_data(journal, name, &data, &length) < 0)
		return false;
	size_t nameLength = strlen(name) + 1;
	if (length < nameLength)
		return false;
	value.SetTo((const char*)data + nameLength, length - nameLength);
	return true;
}


void
CoredumpServer::_Listener()
{
	sd_journal* journal;
	int status = sd_journal_open(&journal, SD_JOURNAL_LOCAL_ONLY);
	if (status < 0) {
		fprintf(stderr, "debug_server: cannot open the journal: %s\n",
			strerror(-status));
		return;
	}

	BString uid;
	uid.SetToFormat("COREDUMP_UID=%u", (unsigned)getuid());
	sd_journal_add_match(journal, kCoredumpMatch, 0);
	sd_journal_add_match(journal, uid.String(), 0);

	// only the crashes from now on
	sd_journal_seek_tail(journal);
	sd_journal_previous(journal);

	while (true) {
		status = sd_journal_next(journal);
		if (status < 0)
			break;
		if (status == 0) {
			sd_journal_wait(journal, (uint64_t)-1);
			continue;
		}

		BString executable;
		BString name;
		BString pid;
		if (!journal_field(journal, "COREDUMP_PID", pid))
			continue;
		journal_field(journal, "COREDUMP_EXE", executable);
		if (!journal_field(journal, "COREDUMP_COMM", name))
			name = BPath(executable.String()).Leaf();

		printf("debug_server: %s (%s, pid %s) crashed\n", name.String(),
			executable.String(), pid.String());
		handle_crash(executable.String(), name.String(), pid.String());
	}

	sd_journal_close(journal);
}


int
main()
{
	status_t error;
	CoredumpServer server(error);
	if (error != B_OK) {
		fprintf(stderr, "debug_server: Failed to create BApplication: %s\n",
			strerror(error));
		return 1;
	}

	error = server.Init();
	if (error != B_OK) {
		fprintf(stderr, "debug_server: Failed to init application: %s\n",
			strerror(error));
		return 1;
	}

	server.Run();
	return 0;
}
