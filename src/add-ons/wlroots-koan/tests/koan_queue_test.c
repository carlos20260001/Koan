/*
 * Regression test for koan_queue.
 *
 *  - Commands are drained in the order they were pushed. Head insertion
 *    used to reverse them, so app_server applied frames and show/hide
 *    requests newest-first.
 *  - The wake fd is non-blocking. The Wayland thread drains it with read()
 *    until it would block; a blocking fd hung the thread after the first
 *    wake-up.
 */

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "koan_queue.h"

static int sOrder[8];
static int sCount;

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

	koan_queue_destroy(queue);
	printf("PASS: koan_queue FIFO order and non-blocking wake fd\n");
	return 0;
}
