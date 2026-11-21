/*
 * Copyright 2026, air/OS. All rights reserved.
 * Distributed under the terms of the MIT License.
 */


#include "JobProcess.h"

#include <Autolock.h>
#include <Message.h>
#include <image.h>

#include <errno.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/prctl.h>
#include <sys/wait.h>
#include <unistd.h>


// #pragma mark - lines of messages


static BString
to_hex(const BMessage& message)
{
	ssize_t size = message.FlattenedSize();
	char* data = (char*)malloc(size > 0 ? size : 1);
	BString hex;
	if (data != NULL && message.Flatten(data, size) == B_OK) {
		static const char kDigits[] = "0123456789abcdef";
		char* out = hex.LockBuffer(size * 2 + 1);
		for (ssize_t i = 0; i < size; i++) {
			out[i * 2] = kDigits[(uint8)data[i] >> 4];
			out[i * 2 + 1] = kDigits[(uint8)data[i] & 0xf];
		}
		hex.UnlockBuffer(size * 2);
	}
	free(data);
	return hex;
}


static bool
from_hex(const char* hex, BMessage& message)
{
	size_t length = strlen(hex) / 2;
	char* data = (char*)malloc(length > 0 ? length : 1);
	if (data == NULL)
		return false;
	for (size_t i = 0; i < length; i++) {
		char byte[3] = { hex[i * 2], hex[i * 2 + 1], 0 };
		data[i] = (char)strtol(byte, NULL, 16);
	}
	bool ok = message.Unflatten(data) == B_OK;
	free(data);
	return ok;
}


static void
report_to_message(const TranscodeReport& report, BMessage& message)
{
	message.AddString("encoder", report.videoEncoder);
	message.AddString("settings", report.videoSettings);
	message.AddInt32("backend", report.backend);
	message.AddBool("fallback", report.cpuFallback);
	message.AddString("reason", report.fallbackReason);
	message.AddInt32("width", report.width);
	message.AddInt32("height", report.height);
	for (size_t i = 0; i < report.audioTracks.size(); i++)
		message.AddString("audio", report.audioTracks[i]);
	for (size_t i = 0; i < report.subtitleTracks.size(); i++)
		message.AddString("subtitle", report.subtitleTracks[i]);
	message.AddString("notes", report.notes);
}


static void
report_from_message(const BMessage& message, TranscodeReport& report)
{
	report = TranscodeReport();
	report.videoEncoder = message.GetString("encoder", "");
	report.videoSettings = message.GetString("settings", "");
	report.backend = (encoder_backend)message.GetInt32("backend",
		kBackendSoftware);
	report.cpuFallback = message.GetBool("fallback", false);
	report.fallbackReason = message.GetString("reason", "");
	report.width = message.GetInt32("width", 0);
	report.height = message.GetInt32("height", 0);
	const char* line;
	for (int32 i = 0; message.FindString("audio", i, &line) == B_OK; i++)
		report.audioTracks.push_back(line);
	for (int32 i = 0; message.FindString("subtitle", i, &line) == B_OK; i++)
		report.subtitleTracks.push_back(line);
	report.notes = message.GetString("notes", "");
}


// #pragma mark - the window's side


JobProcess::JobProcess(const TranscodeJob& job)
	:
	fJob(job),
	fCrashed(false),
	fRetried(false),
	fLock("job process"),
	fChild(-1),
	fInput(-1),
	fCancelled(false),
	fKillAt(-1)
{
}


JobProcess::~JobProcess()
{
	if (fInput >= 0)
		close(fInput);
}


static BString
own_executable()
{
	image_info info;
	int32 cookie = 0;
	while (get_next_image_info(B_CURRENT_TEAM, &cookie, &info) == B_OK) {
		if (info.type == B_APP_IMAGE)
			return BString(info.name);
	}
	return BString();
}


/*!	A worker that dies on the graphics card's path (the card hung and was
	reset, and Mesa ended the process) is run again on the CPU. */
status_t
JobProcess::Run(TranscodeListener* listener, BString& error)
{
	status_t status = _RunOnce(listener, error);
	if (status == B_OK || !fCrashed
		|| fJob.settings.encoderPreference == kPreferSoftware)
		return status;
	BString gpu = fReport.backend != kBackendSoftware ? fReport.videoEncoder
		: BString("The graphics card");
	unlink(fJob.destination.String());
	fJob.settings.encoderPreference = kPreferSoftware;
	fCrashed = false;
	fRetried = true;
	error = "";
	status = _RunOnce(listener, error);
	if (status == B_OK) {
		BString note;
		note.SetToFormat("%s stopped during the conversion: it was done again "
			"on the CPU.", gpu.String());
		if (!fReport.notes.IsEmpty())
			fReport.notes << "\n";
		fReport.notes << note;
		fReport.cpuFallback = true;
		fReport.fallbackReason = note;
	}
	return status;
}


status_t
JobProcess::_RunOnce(TranscodeListener* listener, BString& error)
{
	{
		BAutolock _(fLock);
		if (fCancelled) {
			error = "Cancelled.";
			return B_CANCELED;
		}
	}
	BString executable = own_executable();
	int input[2];
	int output[2];
	if (executable.IsEmpty() || pipe(input) != 0) {
		error = "The conversion could not be started.";
		return B_ERROR;
	}
	if (pipe(output) != 0) {
		close(input[0]);
		close(input[1]);
		error = "The conversion could not be started.";
		return B_ERROR;
	}
	pid_t child = fork();
	if (child == 0) {
		dup2(input[0], STDIN_FILENO);
		dup2(output[1], STDOUT_FILENO);
		close(input[0]);
		close(input[1]);
		close(output[0]);
		close(output[1]);
		execl(executable.String(), executable.String(), "--worker",
			(char*)NULL);
		_exit(127);
	}
	close(input[0]);
	close(output[1]);
	if (child < 0) {
		close(input[1]);
		close(output[0]);
		error = "The conversion could not be started.";
		return B_ERROR;
	}
	{
		BAutolock _(fLock);
		fChild = child;
		fInput = input[1];
		if (fCancelled) {
			close(fInput);
			fInput = -1;
		}
	}

	// the job, on one line
	BMessage job;
	job.AddString("source", fJob.source);
	job.AddString("destination", fJob.destination);
	BMessage settings;
	fJob.settings.Archive(settings);
	job.AddMessage("settings", &settings);
	BString line;
	line << "job " << to_hex(job) << "\n";
	{
		BAutolock _(fLock);
		if (fInput >= 0) {
			signal(SIGPIPE, SIG_IGN);
			write(fInput, line.String(), line.Length());
		}
	}

	BString pending;
	status_t status = B_ERROR;
	bool finished = false;
	while (true) {
		int timeout = 500;
		{
			BAutolock _(fLock);
			if (fKillAt > 0) {
				bigtime_t left = fKillAt - system_time();
				if (left <= 0) {
					kill(fChild, SIGKILL);
					fKillAt = -1;
				} else
					timeout = (int)(left / 1000) + 1;
			}
		}
		struct pollfd wait = { output[0], POLLIN, 0 };
		int ready = poll(&wait, 1, timeout);
		if (ready < 0 && errno == EINTR)
			continue;
		if (ready == 0)
			continue;
		char buffer[4096];
		ssize_t bytes = read(output[0], buffer, sizeof(buffer));
		if (bytes <= 0)
			break;
		pending.Append(buffer, bytes);
		int32 end;
		while ((end = pending.FindFirst('\n')) >= 0) {
			BString text;
			pending.CopyInto(text, 0, end);
			pending.Remove(0, end + 1);
			if (text.StartsWith("progress ")) {
				TranscodeProgress progress;
				memset(&progress, 0, sizeof(progress));
				long long elapsed, remaining, frames, written;
				if (sscanf(text.String() + 9, "%f %lf %lf %lld %lld %lld %lld",
						&progress.fraction, &progress.framesPerSecond,
						&progress.speed, &elapsed, &remaining, &frames,
						&written) == 7) {
					progress.elapsed = elapsed;
					progress.remaining = remaining;
					progress.frames = frames;
					progress.bytesWritten = written;
					if (listener != NULL)
						listener->TranscodeProgressed(progress);
				}
			} else if (text.StartsWith("started ")) {
				BMessage message;
				if (from_hex(text.String() + 8, message)) {
					report_from_message(message, fReport);
					if (listener != NULL)
						listener->TranscodeStarted(fReport);
				}
			} else if (text.StartsWith("finished ")) {
				// finished <status> <report> <error>
				char* rest = NULL;
				status = (status_t)strtol(text.String() + 9, &rest, 10);
				BString fields(rest != NULL ? rest : "");
				fields.Trim();
				int32 space = fields.FindFirst(' ');
				BString reportHex = fields;
				BString errorHex;
				if (space >= 0) {
					fields.CopyInto(reportHex, 0, space);
					fields.CopyInto(errorHex, space + 1,
						fields.Length() - space - 1);
				}
				BMessage message;
				if (from_hex(reportHex.String(), message))
					report_from_message(message, fReport);
				BMessage errorMessage;
				if (!errorHex.IsEmpty()
					&& from_hex(errorHex.String(), errorMessage))
					error = errorMessage.GetString("error", "");
				finished = true;
			}
		}
	}
	close(output[0]);
	int exitStatus = 0;
	waitpid(child, &exitStatus, 0);
	{
		BAutolock _(fLock);
		fChild = -1;
		if (fInput >= 0) {
			close(fInput);
			fInput = -1;
		}
	}
	if (!finished) {
		fCrashed = !fCancelled;
		if (fCancelled) {
			error = "Cancelled.";
			return B_CANCELED;
		}
		error = WIFSIGNALED(exitStatus)
			? "The conversion stopped unexpectedly (it crashed)."
			: "The conversion stopped unexpectedly.";
		return B_ERROR;
	}
	return status;
}


void
JobProcess::Cancel()
{
	BAutolock _(fLock);
	fCancelled = true;
	// the worker stops when its input closes, and is ended if it has not
	// after a few seconds
	if (fInput >= 0) {
		close(fInput);
		fInput = -1;
	}
	if (fChild >= 0 && fKillAt < 0)
		fKillAt = system_time() + 10000000;
}


// #pragma mark - the worker


class WorkerListener : public TranscodeListener {
public:
	virtual void TranscodeStarted(const TranscodeReport& report)
	{
		BMessage message;
		report_to_message(report, message);
		BString line;
		line << "started " << to_hex(message) << "\n";
		fputs(line.String(), stdout);
		fflush(stdout);
	}

	virtual void TranscodeProgressed(const TranscodeProgress& progress)
	{
		printf("progress %f %f %f %lld %lld %lld %lld\n", progress.fraction,
			progress.framesPerSecond, progress.speed,
			(long long)progress.elapsed, (long long)progress.remaining,
			(long long)progress.frames, (long long)progress.bytesWritten);
		fflush(stdout);
	}
};


static status_t
watch_input(void* data)
{
	// the window closed our input: stop
	char buffer[256];
	while (read(STDIN_FILENO, buffer, sizeof(buffer)) > 0)
		;
	((Transcoder*)data)->Cancel();
	return B_OK;
}


/*static*/ int
JobProcess::WorkerMain()
{
	// a crash ends this process at once, for the window to notice
	// (Linux: no core dump, so systemd-coredump and debug_server's crash
	// dialog leave it alone; libroot2 has no disable_debugger())
	prctl(PR_SET_DUMPABLE, 0, 0, 0, 0);

	BString line;
	char c;
	while (read(STDIN_FILENO, &c, 1) == 1 && c != '\n')
		line << c;
	BMessage message;
	if (!line.StartsWith("job ") || !from_hex(line.String() + 4, message))
		return 2;
	TranscodeJob job;
	job.source = message.GetString("source", "");
	job.destination = message.GetString("destination", "");
	BMessage settings;
	if (message.FindMessage("settings", &settings) == B_OK)
		job.settings.Unarchive(settings);

	Transcoder transcoder(job);
	thread_id watcher = spawn_thread(watch_input, "watch input",
		B_NORMAL_PRIORITY, &transcoder);
	resume_thread(watcher);

	WorkerListener listener;
	BString error;
	status_t status = transcoder.Run(&listener, error);
	// stopped (or the window gone): what was written is not a whole file
	if (status == B_CANCELED)
		unlink(job.destination.String());

	BMessage report;
	report_to_message(transcoder.Report(), report);
	BMessage errorMessage;
	errorMessage.AddString("error", error);
	BString out;
	out << "finished " << (int32)status << " " << to_hex(report) << " "
		<< to_hex(errorMessage) << "\n";
	fputs(out.String(), stdout);
	fflush(stdout);
	// the watcher waits on our input: leave without it
	_exit(0);
}
