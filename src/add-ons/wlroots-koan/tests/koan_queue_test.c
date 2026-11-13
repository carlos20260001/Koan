/*
 * SPDX-License-Identifier: MIT
 * Copyright © 2026 carlos20260001
 */

/*
 * Regression test for koan_queue.
 *
 *  - Commands are drained in the order they were pushed. Head insertion
 *    used to reverse them, so app_server applied frames and show/hide
 *    requests newest-first.
 *  - The wake fd is non-blocking. The Wayland thread drains it with read()
 *    until it would block; a blocking fd hung the thread after the first
 *    wake-up.
 *  - A frame (SET_BUFFER) replaces the same window's frame that is still
 *    queued. A client committing faster than app_server drained queued one
 *    full-window copy per commit, without bound.
 */

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "koan_frame.h"
#include "koan_queue.h"

static int sOrder[8];
static int sCount;

/* Frames drained, as "<window><frame>": window 'a' or 'b', frame '1'..'9'
 * from the first byte of its data; other commands as 'v'. */
static char sFrames[16];
static int sFrameCount;

static int
record_frame(struct koan_command *cmd, void *userdata)
{
	(void)userdata;
	if (sFrameCount + 2 >= (int)sizeof(sFrames))
		return 0;
	if (cmd->type != KOAN_CMD_SET_BUFFER) {
		sFrames[sFrameCount++] = 'v';
		return 0;
	}
	sFrames[sFrameCount++]
		= (char)(uintptr_t)cmd->payload.set_buffer.window;
	sFrames[sFrameCount++] = *(const char *)cmd->payload.set_buffer.data;
	return 0;
}

static bool
push_frame(struct koan_queue *queue, char window, char frame)
{
	union koan_cmd_payload payload = {0};
	payload.set_buffer.window = (void *)(uintptr_t)window;
	payload.set_buffer.data = koan_frame_alloc(1);
	if (payload.set_buffer.data == NULL)
		return false;
	*(char *)payload.set_buffer.data = frame;
	payload.set_buffer.width = 1;
	payload.set_buffer.height = 1;
	payload.set_buffer.stride = 4;
	return koan_queue_push(queue, KOAN_CMD_SET_BUFFER, &payload, NULL)
		!= NULL;
}

static int
record(struct koan_command *cmd, void *userdata)
{
	(void)userdata;
	if (sCount < 8)
		sOrder[sCount++] = cmd->payload.set_visibility.visible ? 1 : 0;
	return 0;
}

int
main(void)
{
	struct koan_queue *queue = koan_queue_create();
	if (queue == NULL) {
		fprintf(stderr, "FAIL: koan_queue_create() returned NULL\n");
		return 1;
	}

	/* hide, hide, show: the final state must be "shown". Not a
	 * palindrome, so a reversed drain is caught. */
	const int pushed[] = { 0, 0, 1 };
	for (int i = 0; i < 3; i++) {
		union koan_cmd_payload payload = {0};
		payload.set_visibility.visible = pushed[i] != 0;
		if (koan_queue_push(queue, KOAN_CMD_SET_VISIBILITY, &payload,
				NULL) == NULL) {
			fprintf(stderr, "FAIL: koan_queue_push() failed\n");
			return 1;
		}
	}

	koan_queue_drain_direction(queue, KOAN_QUEUE_TO_APP_SERVER, record, NULL);
	if (sCount != 3 || memcmp(sOrder, pushed, sizeof(pushed)) != 0) {
		fprintf(stderr, "FAIL: commands drained out of order "
			"(%d: %d %d %d)\n", sCount, sOrder[0], sOrder[1], sOrder[2]);
		return 1;
	}

	int fd = koan_queue_get_wake_fd(queue);
	if (fd < 0 || !(fcntl(fd, F_GETFL) & O_NONBLOCK)) {
		fprintf(stderr, "FAIL: wake fd is missing or blocking\n");
		return 1;
	}

	/* Drain the three wake bytes, then the next read must not block. */
	char buf[16];
	while (read(fd, buf, sizeof(buf)) > 0)
		;
	if (errno != EAGAIN && errno != EWOULDBLOCK) {
		fprintf(stderr, "FAIL: draining the wake fd: %s\n", strerror(errno));
		return 1;
	}

	/* a1 b1 (show) a2 a3: a1 and a2 are superseded before app_server
	 * drains, so it gets b1, the show request and a3, in that order.
	 * The newest frame goes to the tail so it never overtakes a command
	 * pushed before it. */
	union koan_cmd_payload show = {0};
	show.set_visibility.window = (void *)(uintptr_t)'a';
	show.set_visibility.visible = true;
	if (!push_frame(queue, 'a', '1') || !push_frame(queue, 'b', '1')
		|| koan_queue_push(queue, KOAN_CMD_SET_VISIBILITY, &show, NULL)
			== NULL
		|| !push_frame(queue, 'a', '2') || !push_frame(queue, 'a', '3')) {
		fprintf(stderr, "FAIL: pushing frames failed\n");
		return 1;
	}
	koan_queue_drain_direction(queue, KOAN_QUEUE_TO_APP_SERVER,
		record_frame, NULL);
	sFrames[sFrameCount] = '\0';
	if (strcmp(sFrames, "b1va3") != 0) {
		fprintf(stderr, "FAIL: frames drained as \"%s\", expected "
			"\"b1va3\" (one pending frame per window, newest)\n", sFrames);
		return 1;
	}

	koan_queue_destroy(queue);
	printf("PASS: koan_queue FIFO order, non-blocking wake fd, one pending "
		"frame per window\n");
	return 0;
}
