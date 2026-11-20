/*
 * Copyright 2003-2015, Axel Dörfler, axeld@pinc-software.de.
 * Copyright 2026, Koan contributors. All rights reserved.
 * Distributed under the terms of the MIT License.
 */


#include "SyslogDaemon.h"

#include <stdio.h>
#include <string.h>

#include <Alert.h>
#include <Catalog.h>
#include <FindDirectory.h>
#include <Font.h>
#include <Path.h>
#include <TextView.h>

#include <LaunchRoster.h>
#include <syscalls.h>
#include <syslog_daemon.h>

#ifdef __VOS__
#	include <stdlib.h>
#	include <syslog.h>
#	include <systemd/sd-journal.h>
#endif

#include "listener_output.h"
#include "syslog_output.h"


#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "SyslogDaemon"


static const int32 kQuitDaemon = 'quit';


SyslogDaemon::SyslogDaemon()
	:
	BServer(B_SYSTEM_LOGGER_SIGNATURE, false, NULL),
	fHandlerLock("handler lock")
{
}


void
SyslogDaemon::ReadyToRun()
{
#ifdef __VOS__
	// On Linux, syslog() and the kernel log go to the systemd journal: the
	// daemon reads it (see _Daemon()) and hands each message to its
	// handlers, the log file and the listeners. The port only tells the
	// daemon thread to quit.
	fPort = create_port(1, "syslog daemon");
#else
	fPort = BLaunchRoster().GetPort("logger");
#endif
	fDaemon = spawn_thread(_DaemonThread, "daemon", B_NORMAL_PRIORITY, this);

	if (fPort >= 0 && fDaemon >= 0) {
#ifndef __VOS__
		_kern_register_syslog_daemon(fPort);
#endif

		init_syslog_output(this);
		init_listener_output(this);

		resume_thread(fDaemon);
	} else
		Quit();
}


void
SyslogDaemon::AboutRequested()
{
	BPath path;
	find_directory(B_SYSTEM_LOG_DIRECTORY, &path);
	path.Append("syslog");

	BString name(B_TRANSLATE("Syslog Daemon"));
	BString message;
	snprintf(message.LockBuffer(512), 512,
		B_TRANSLATE("%s\n\nThis daemon collects all system messages and writes them to the "
			"system-wide log at \"%s\".\n\n"), name.String(), path.Path());
	message.UnlockBuffer();

	BAlert* alert = new BAlert(name.String(), message.String(),
		B_TRANSLATE("OK"));
	BTextView* view = alert->TextView();
	BFont font;

	view->SetStylable(true);

	view->GetFont(&font);
	font.SetSize(21);
	font.SetFace(B_BOLD_FACE);
	view->SetFontAndColor(0, name.Length(), &font);

	alert->SetFlags(alert->Flags() | B_CLOSE_ON_ESCAPE);
	alert->Go(NULL);
}


bool
SyslogDaemon::QuitRequested()
{
	write_port(fPort, kQuitDaemon, NULL, 0);
	wait_for_thread(fDaemon, NULL);

	return true;
}


void
SyslogDaemon::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case SYSLOG_ADD_LISTENER:
		{
			BMessenger messenger;
			if (message->FindMessenger("target", &messenger) == B_OK)
				add_listener(&messenger);
			break;
		}
		case SYSLOG_REMOVE_LISTENER:
		{
			BMessenger messenger;
			if (message->FindMessenger("target", &messenger) == B_OK)
				remove_listener(&messenger);
			break;
		}

		default:
			BApplication::MessageReceived(message);
	}
}


void
SyslogDaemon::AddHandler(handler_func function)
{
	fHandlers.AddItem((void*)function);
}


#ifdef __VOS__
/*!	Copies a journal field ("NAME=value") into \a buffer without its name;
	returns false when the entry does not have it. */
static bool
journal_field(sd_journal* journal, const char* name, char* buffer,
	size_t size)
{
	const void* data;
	size_t length;
	if (sd_journal_get_data(journal, name, &data, &length) < 0)
		return false;

	size_t nameLength = strlen(name) + 1;
	if (length < nameLength)
		return false;
	length -= nameLength;
	if (length >= size)
		length = size - 1;
	memcpy(buffer, (const char*)data + nameLength, length);
	buffer[length] = '\0';
	return true;
}


void
SyslogDaemon::_Daemon()
{
	char buffer[SYSLOG_MESSAGE_BUFFER_SIZE + 1];
	syslog_message& message = *(syslog_message*)buffer;

	sd_journal* journal;
	int status = sd_journal_open(&journal, SD_JOURNAL_LOCAL_ONLY);
	if (status < 0) {
		fprintf(stderr, "syslog_daemon: cannot open the journal: %s\n",
			strerror(-status));
		return;
	}
	// only what is logged from now on
	sd_journal_seek_tail(journal);
	sd_journal_previous(journal);

	while (true) {
		if (port_count(fPort) > 0) {
			int32 code;
			if (read_port(fPort, &code, NULL, 0) < 0 || code == kQuitDaemon)
				break;
		}

		status = sd_journal_next(journal);
		if (status < 0)
			break;
		if (status == 0) {
			sd_journal_wait(journal, 500000);
			continue;
		}

		char field[64];
		memset(&message, 0, sizeof(syslog_message));
		if (!journal_field(journal, "MESSAGE", message.message,
				SYSLOG_MAX_MESSAGE_LENGTH) || message.message[0] == '\0')
			continue;

		if (!journal_field(journal, "SYSLOG_IDENTIFIER", message.ident,
				sizeof(message.ident))) {
			journal_field(journal, "_COMM", message.ident,
				sizeof(message.ident));
		}
		if (journal_field(journal, "_PID", field, sizeof(field)))
			message.from = strtol(field, NULL, 10);

		int priority = LOG_INFO;
		if (journal_field(journal, "PRIORITY", field, sizeof(field)))
			priority = strtol(field, NULL, 10) & LOG_PRIMASK;
		int facility = LOG_USER;
		if (journal_field(journal, "SYSLOG_FACILITY", field, sizeof(field)))
			facility = strtol(field, NULL, 10) << 3;
		else if (journal_field(journal, "_TRANSPORT", field, sizeof(field))
			&& strcmp(field, "kernel") == 0)
			facility = LOG_KERN;
		message.priority = priority;
		message.options = facility | priority;

		uint64_t usecs;
		if (sd_journal_get_realtime_usec(journal, &usecs) >= 0)
			message.when = usecs / 1000000;
		else
			message.when = time(NULL);

		fHandlerLock.Lock();
		for (int32 i = fHandlers.CountItems(); i-- > 0;) {
			handler_func handle = (handler_func)fHandlers.ItemAt(i);
			handle(message);
		}
		fHandlerLock.Unlock();
	}

	sd_journal_close(journal);
}
#else	// !__VOS__
void
SyslogDaemon::_Daemon()
{
	char buffer[SYSLOG_MESSAGE_BUFFER_SIZE + 1];
	syslog_message& message = *(syslog_message*)buffer;
	int32 code;

	while (true) {
		ssize_t bytesRead = read_port(fPort, &code, &message, sizeof(buffer));
		if (bytesRead == B_BAD_PORT_ID) {
			// we've been quit
			break;
		}

		if (code == kQuitDaemon)
			return;

		// if we don't get what we want, ignore it
		if (bytesRead < (ssize_t)sizeof(syslog_message)
			|| code != SYSLOG_MESSAGE)
			continue;

		// add terminating null byte
		message.message[bytesRead - sizeof(syslog_message)] = '\0';

		if (!message.message[0]) {
			// ignore empty messages
			continue;
		}

		fHandlerLock.Lock();

		for (int32 i = fHandlers.CountItems(); i-- > 0;) {
			handler_func handle = (handler_func)fHandlers.ItemAt(i);

			handle(message);
		}

		fHandlerLock.Unlock();
	}
}
#endif	// !__VOS__


int32
SyslogDaemon::_DaemonThread(void* data)
{
	((SyslogDaemon*)data)->_Daemon();
	return B_OK;
}


// #pragma mark -


int
main(int argc, char** argv)
{
	SyslogDaemon daemon;
	daemon.Run();

	return 0;
}
