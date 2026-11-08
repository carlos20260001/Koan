/*
 * Regression test: the Koan Wayland server must start up without aborting.
 *
 * app_server calls koan_server_create(), koan_server_add_socket() and
 * koan_server_start() from main() before entering its message loop. Any
 * wlroots assertion on that path kills app_server, janus gives up after
 * three restarts and the desktop stays black. This test drives the same
 * sequence (plus one dispatch and teardown) in a plain host process.
 *
 * Exits 0 on success, 1 on a reported failure; an assertion aborts the
 * process, which also counts as a failure.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <wlr/util/log.h>

#include "koan_server.h"

int
main(void)
{
	char runtime_dir[] = "/tmp/koan-server-test-XXXXXX";
	struct wl_display *display;

	wlr_log_init(WLR_DEBUG, NULL);

	/* wl_display_add_socket_auto() needs a private XDG_RUNTIME_DIR. */
	if (mkdtemp(runtime_dir) == NULL) {
		perror("mkdtemp");
		return 1;
	}
	setenv("XDG_RUNTIME_DIR", runtime_dir, 1);

	display = koan_server_create();
	if (display == NULL) {
		fprintf(stderr, "FAIL: koan_server_create() returned NULL\n");
		return 1;
	}

	/* janus_session exports WAYLAND_DISPLAY to the whole session; the
	 * server must bind exactly that name or clients cannot find it. */
	setenv("WAYLAND_DISPLAY", "koan-test-0", 1);
	const char *socket = koan_server_add_socket(display);
	if (socket == NULL) {
		fprintf(stderr, "FAIL: koan_server_add_socket() returned NULL\n");
		koan_server_destroy(display);
		return 1;
	}
	if (strcmp(socket, "koan-test-0") != 0) {
		fprintf(stderr, "FAIL: socket is '%s', not $WAYLAND_DISPLAY "
			"(koan-test-0)\n", socket);
		koan_server_destroy(display);
		return 1;
	}

	if (!koan_server_start()) {
		fprintf(stderr, "FAIL: koan_server_start() returned false\n");
		koan_server_destroy(display);
		return 1;
	}

	if (!koan_server_dispatch(display, 0)) {
		fprintf(stderr, "FAIL: koan_server_dispatch() returned false\n");
		koan_server_destroy(display);
		return 1;
	}

	koan_server_destroy(display);
	rmdir(runtime_dir);

	printf("PASS: koan server create/start/dispatch/destroy\n");
	return 0;
}
