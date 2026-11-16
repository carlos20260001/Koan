/*
 * Copyright 2026, Koan contributors. All rights reserved.
 * Distributed under the terms of the MIT License.
 */


// vos-splash: the Haiku boot splash on Linux.
//
//   vos-splash --daemon [--lit N] [--handoff-timeout S]
//		Draws the logo and the icon row, lights stages 1..N, and keeps the
//		display, reading commands from /run/vos-splash.fifo. N is a floor;
//		a higher stage recorded in /run/vos-splash.stage wins. Returns once the
//		splash is on screen (or exits 0 at once when there is no display, or
//		when another daemon already serves the FIFO).
//   vos-splash --stage N	lights stages up to N; 7 also hands off
//   vos-splash --deactivate	drops DRM master, keeps the picture
//   vos-splash --quit		exits the daemon, leaving the picture
//
// Commands are lines on the FIFO: "1".."7", "deactivate", "quit".
//
// Started in the initramfs, the daemon survives the switch to the real root
// the way plymouthd does: run-init does not kill it, and the initramfs moves
// /dev and /run onto the new root. Paths stop working for it then ("/dev"
// names the emptied initramfs), so it opens /dev and /run as directory fds
// first and uses only those.

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include <boot_splash.h>

#include "splash_display.h"
#include "splash_platform.h"
#include "splash_stages.h"


static const char* kRunDir = "/run";
static const char* kFifoName = "vos-splash.fifo";
static const char* kStageName = "vos-splash.stage";
	// highest stage lit, for a daemon restarted on the real root
static const char* kInactiveName = "vos-splash.inactive";
	// written once DRM master is dropped: --stage 7 waits for it

static const int kPollMilliseconds = 250;
static const int kDefaultHandoffTimeout = 15;
static const int kClientWaitMilliseconds = 3000;

static volatile sig_atomic_t sTerminate = 0;


static void
usage(FILE* out)
{
	fprintf(out,
		"usage: vos-splash --daemon [--lit N] [--handoff-timeout SECONDS]\n"
		"       vos-splash --stage N        (N = 1..7)\n"
		"       vos-splash --deactivate\n"
		"       vos-splash --quit\n");
}


static int64
now_ms()
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (int64)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}


static void
on_terminate(int)
{
	sTerminate = 1;
}


static void
write_state(int runDir, const char* name, const char* text)
{
	char temp[64];
	snprintf(temp, sizeof(temp), ".%s.tmp", name);
	int fd = openat(runDir, temp, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC,
		0644);
	if (fd < 0)
		return;
	ssize_t length = (ssize_t)strlen(text);
	bool ok = write(fd, text, length) == length;
	close(fd);
	if (ok)
		renameat(runDir, temp, runDir, name);
	else
		unlinkat(runDir, temp, 0);
}


static int
read_stage_state(int runDir)
{
	int fd = openat(runDir, kStageName, O_RDONLY | O_CLOEXEC);
	if (fd < 0)
		return -1;
	char buffer[16];
	ssize_t length = read(fd, buffer, sizeof(buffer) - 1);
	close(fd);
	if (length <= 0)
		return -1;
	buffer[length] = '\0';
	return vos_splash_parse_stage(buffer);
}


// #pragma mark - daemon


struct splash_state {
	int				devDir;
	int				runDir;
	int				fifo;
	splash_display	display;
	uint8*			icons;
	int				lit;
		// highest stage lit, 0 for none
	bool			deactivated;
	int64			handoffDeadline;
	int				handoffTimeout;
};


static void
light_stages(int from, int to)
{
	for (int stage = from; stage <= to; stage++)
		boot_splash_set_stage(vos_splash_stage_icon(stage));
}


static status_t
draw(splash_state& state)
{
	boot_splash_uninit();
	free(state.icons);
	state.icons = NULL;

	status_t status = video_display_splash(state.display.frame_buffer,
		&state.icons);
	if (status != B_OK)
		return status;

	boot_splash_init(state.icons, &state.display.frame_buffer);
	light_stages(VOS_SPLASH_STAGE_INITRAMFS, state.lit);
	status = splash_display_show(&state.display);
	splash_display_flush(&state.display);
	return status;
}


static void
deactivate(splash_state& state)
{
	if (!state.deactivated) {
		splash_display_drop_master(&state.display);
		state.deactivated = true;
		state.handoffDeadline = now_ms()
			+ (int64)state.handoffTimeout * 1000;
	}
	// Answer every request, also repeated ones: the client waits for it.
	write_state(state.runDir, kInactiveName, "1\n");
}


static void
set_stage(splash_state& state, int stage)
{
	if (stage > state.lit) {
		if (state.display.kind != SPLASH_DISPLAY_NONE && !state.deactivated) {
			light_stages(state.lit + 1, stage);
			splash_display_flush(&state.display);
		}
		state.lit = stage;

		char text[16];
		snprintf(text, sizeof(text), "%d\n", stage);
		write_state(state.runDir, kStageName, text);
	}

	if (stage == VOS_SPLASH_STAGE_APP_SERVER)
		deactivate(state);
}


// Returns false on "quit".
static bool
handle_command(splash_state& state, char* line)
{
	if (strcmp(line, "quit") == 0)
		return false;
	if (strcmp(line, "deactivate") == 0) {
		deactivate(state);
		return true;
	}

	int stage = vos_splash_parse_stage(line);
	if (stage > 0)
		set_stage(state, stage);
	else if (line[0] != '\0')
		fprintf(stderr, "vos-splash: unknown command \"%s\"\n", line);
	return true;
}


static bool
read_commands(splash_state& state, char* buffer, size_t& used,
	size_t capacity)
{
	while (true) {
		ssize_t length = read(state.fifo, buffer + used, capacity - used);
		if (length < 0) {
			if (errno == EINTR)
				continue;
			return true;
		}
		if (length == 0)
			return true;
		used += length;

		char* start = buffer;
		char* newline;
		while ((newline = (char*)memchr(start, '\n',
				buffer + used - start)) != NULL) {
			*newline = '\0';
			if (!handle_command(state, start))
				return false;
			start = newline + 1;
		}
		used = buffer + used - start;
		memmove(buffer, start, used);
		if (used == capacity)
			used = 0;
				// a line that long is garbage
	}
}


static void
run_daemon(splash_state& state)
{
	char buffer[256];
	size_t used = 0;

	while (!sTerminate) {
		pollfd pfd = { state.fifo, POLLIN, 0 };
		int result = poll(&pfd, 1, kPollMilliseconds);
		if (result < 0 && errno != EINTR)
			break;
		if (result > 0 && (pfd.revents & POLLIN) != 0
			&& !read_commands(state, buffer, used, sizeof(buffer)))
			break;

		if (state.deactivated) {
			// Waiting for app_server to say it has the display; after the
			// timeout assume it does (or never will) and leave.
			if (now_ms() >= state.handoffDeadline)
				break;
			continue;
		}

		// simpledrm or efifb gave way to the real driver: start over on
		// whatever device exists now.
		if (splash_display_lost(&state.display, state.devDir)) {
			splash_display_close(&state.display, true);
			if (splash_display_open(&state.display, state.devDir) == B_OK)
				draw(state);
		}
	}
}


static int
start_daemon(int lit, int handoffTimeout)
{
	// Another daemon serves the FIFO (the initramfs one survived the pivot):
	// nothing to do. Opening a FIFO for writing without a reader is ENXIO.
	char path[64];
	snprintf(path, sizeof(path), "%s/%s", kRunDir, kFifoName);
	int probe = open(path, O_WRONLY | O_NONBLOCK | O_CLOEXEC);
	if (probe >= 0) {
		close(probe);
		return 0;
	}

	splash_state state;
	memset(&state, 0, sizeof(state));
	state.fifo = -1;
	state.handoffTimeout = handoffTimeout;

	state.devDir = open("/dev", O_PATH | O_DIRECTORY | O_CLOEXEC);
	state.runDir = open(kRunDir, O_PATH | O_DIRECTORY | O_CLOEXEC);
	if (state.devDir < 0 || state.runDir < 0) {
		fprintf(stderr, "vos-splash: cannot open /dev or %s: %s\n", kRunDir,
			strerror(errno));
		return 0;
	}

	if (splash_display_open(&state.display, state.devDir) != B_OK) {
		// No DRM device and no fbdev: nothing to draw on, not an error.
		return 0;
	}

	// --lit is a floor: a daemon restarted on the real root lights at least
	// what the initramfs one recorded.
	int recorded = read_stage_state(state.runDir);
	if (recorded > lit)
		lit = recorded;
	if (lit < VOS_SPLASH_STAGE_INITRAMFS)
		lit = VOS_SPLASH_STAGE_INITRAMFS;
	state.lit = lit;

	// The parent returns once the splash is up and the FIFO is served, so
	// a --stage right after --daemon is never lost.
	int ready[2];
	if (pipe2(ready, O_CLOEXEC) != 0)
		return 1;

	pid_t child = fork();
	if (child < 0)
		return 1;
	if (child > 0) {
		close(ready[1]);
		char status = 1;
		if (read(ready[0], &status, 1) != 1)
			status = 1;
		close(ready[0]);
		// The child keeps the display; this copy of the fds goes.
		return status;
	}
	close(ready[0]);
	setsid();
	if (chdir("/") != 0) {
		// harmless: no relative paths are used
	}

	int null = open("/dev/null", O_RDWR | O_CLOEXEC);
	if (null >= 0) {
		dup2(null, STDIN_FILENO);
		dup2(null, STDOUT_FILENO);
		if (null > STDERR_FILENO)
			close(null);
	}

	struct sigaction action;
	memset(&action, 0, sizeof(action));
	action.sa_handler = on_terminate;
	sigaction(SIGTERM, &action, NULL);
	sigaction(SIGINT, &action, NULL);
	signal(SIGPIPE, SIG_IGN);
	signal(SIGHUP, SIG_IGN);

	unlinkat(state.runDir, kInactiveName, 0);
	unlinkat(state.runDir, kFifoName, 0);
	char status = 1;
	if (mkfifoat(state.runDir, kFifoName, 0600) == 0) {
		// O_RDWR: the FIFO never reports EOF between clients.
		state.fifo = openat(state.runDir, kFifoName,
			O_RDWR | O_NONBLOCK | O_CLOEXEC);
	}
	if (state.fifo >= 0 && draw(state) == B_OK)
		status = 0;

	if (status == 0) {
		char text[16];
		snprintf(text, sizeof(text), "%d\n", state.lit);
		write_state(state.runDir, kStageName, text);
	}

	if (write(ready[1], &status, 1) != 1) {
		// the parent went away; keep going
	}
	close(ready[1]);

	if (status == 0)
		run_daemon(state);

	// Leave the picture: closing the frame buffer is all app_server needs.
	// The FIFO goes last, since --quit waits for it to know the display is
	// released.
	boot_splash_uninit();
	free(state.icons);
	splash_display_close(&state.display, state.deactivated);
	unlinkat(state.runDir, kInactiveName, 0);
	unlinkat(state.runDir, kFifoName, 0);
	if (state.fifo >= 0)
		close(state.fifo);
	_exit(status);
}


// #pragma mark - client


static bool
file_exists(const char* name)
{
	char path[64];
	snprintf(path, sizeof(path), "%s/%s", kRunDir, name);
	return access(path, F_OK) == 0;
}


static int
send_command(const char* command)
{
	char path[64];
	snprintf(path, sizeof(path), "%s/%s", kRunDir, kFifoName);
	int fd = open(path, O_WRONLY | O_NONBLOCK | O_CLOEXEC);
	if (fd < 0) {
		// No daemon (no splash on this boot, or it quit already): fine.
		if (errno == ENOENT || errno == ENXIO)
			return 0;
		fprintf(stderr, "vos-splash: %s: %s\n", path, strerror(errno));
		return 1;
	}

	bool waitForHandoff = strcmp(command, "deactivate") == 0
		|| vos_splash_parse_stage(command) == VOS_SPLASH_STAGE_APP_SERVER;
	bool waitForExit = strcmp(command, "quit") == 0;
	if (waitForHandoff) {
		char inactive[64];
		snprintf(inactive, sizeof(inactive), "%s/%s", kRunDir,
			kInactiveName);
		unlink(inactive);
	}

	char line[32];
	int length = snprintf(line, sizeof(line), "%s\n", command);
	// Lines this short are written atomically into a pipe.
	ssize_t written = write(fd, line, length);
	close(fd);
	if (written != length) {
		fprintf(stderr, "vos-splash: write: %s\n", strerror(errno));
		return 1;
	}

	// app_server must not try drmSetMaster before the daemon dropped it;
	// whoever runs app_server after --quit wants the daemon gone.
	if (waitForHandoff || waitForExit) {
		int64 deadline = now_ms() + kClientWaitMilliseconds;
		while (now_ms() < deadline) {
			if (waitForHandoff && file_exists(kInactiveName))
				return 0;
			if (waitForExit && !file_exists(kFifoName))
				return 0;
			usleep(10 * 1000);
		}
		fprintf(stderr, "vos-splash: no answer from the daemon\n");
	}
	return 0;
}


int
main(int argc, char** argv)
{
	if (argc < 2) {
		usage(stderr);
		return 2;
	}

	if (strcmp(argv[1], "--daemon") == 0) {
		int lit = -1;
		int handoffTimeout = kDefaultHandoffTimeout;
		for (int i = 2; i < argc; i++) {
			if (strcmp(argv[i], "--lit") == 0 && i + 1 < argc) {
				lit = vos_splash_parse_stage(argv[++i]);
				if (lit < 0) {
					usage(stderr);
					return 2;
				}
			} else if (strcmp(argv[i], "--handoff-timeout") == 0
				&& i + 1 < argc) {
				handoffTimeout = atoi(argv[++i]);
				if (handoffTimeout <= 0)
					handoffTimeout = kDefaultHandoffTimeout;
			} else {
				usage(stderr);
				return 2;
			}
		}
		return start_daemon(lit, handoffTimeout);
	}

	if (strcmp(argv[1], "--stage") == 0 && argc == 3) {
		if (vos_splash_parse_stage(argv[2]) < 0) {
			usage(stderr);
			return 2;
		}
		return send_command(argv[2]);
	}
	if (strcmp(argv[1], "--deactivate") == 0 && argc == 2)
		return send_command("deactivate");
	if (strcmp(argv[1], "--quit") == 0 && argc == 2)
		return send_command("quit");
	if (strcmp(argv[1], "--help") == 0) {
		usage(stdout);
		return 0;
	}

	usage(stderr);
	return 2;
}
