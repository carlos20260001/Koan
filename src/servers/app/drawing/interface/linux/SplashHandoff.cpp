/*
 * Copyright 2026, Koan contributors. All rights reserved.
 * Distributed under the terms of the MIT License.
 */


#include "SplashHandoff.h"

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>


static const char* kSplashFifo = "/run/vos-splash.fifo";


void
vos_splash_handoff()
{
	static int32_t sDone = 0;
	if (__atomic_exchange_n(&sDone, 1, __ATOMIC_ACQ_REL) != 0)
		return;

	// "quit" on the daemon's FIFO, as "vos-splash --quit" sends it, without
	// waiting for the daemon to go: the picture it leaves is ours to draw
	// over now. No FIFO, or no reader on it (ENXIO): there is no splash.
	int fd = open(kSplashFifo, O_WRONLY | O_NONBLOCK | O_CLOEXEC);
	if (fd < 0) {
		if (errno != ENOENT && errno != ENXIO) {
			fprintf(stderr, "app_server: boot splash FIFO: %s\n",
				strerror(errno));
		}
		return;
	}

	// A reader that went away between the open and the write must not
	// kill app_server.
	struct sigaction ignore = {};
	struct sigaction previous;
	ignore.sa_handler = SIG_IGN;
	sigaction(SIGPIPE, &ignore, &previous);

	static const char kQuit[] = "quit\n";
	if (write(fd, kQuit, sizeof(kQuit) - 1) < 0)
		fprintf(stderr, "app_server: boot splash quit: %s\n", strerror(errno));

	sigaction(SIGPIPE, &previous, NULL);
	close(fd);
}
