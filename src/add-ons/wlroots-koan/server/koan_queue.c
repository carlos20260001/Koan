/*
 * Koan thread-safe command queue implementation.
 *
 * Threading model:
 *  - Either thread pushes commands via koan_queue_push() and wakes the
 *    Wayland thread by writing a byte to the wake-up pipe.
 *  - Wayland thread registers the pipe read end with wl_event_loop_add_fd().
 *    When readable, it drains the pipe and processes only commands addressed
 *    to it (KOAN_QUEUE_TO_WAYLAND).
 *  - app_server processes only commands addressed to it
 *    (KOAN_QUEUE_TO_APP_SERVER). Commands are never consumed by the wrong
 *    thread.
 *
 * Buffer ownership: payload memory passed via koan_queue_push() is copied
 * into queue-owned heap memory where necessary (e.g. strings, bitmap data).
 * The receiver (processor callback) is responsible for freeing any heap
 * memory it takes ownership of. The queue frees payload memory after the
 * processor returns, unless the processor "steals" it (sets a take flag).
 */

#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>

#include <wlr/util/log.h>

#include "koan_queue.h"

struct koan_queue {
	pthread_mutex_t mutex;
	pthread_cond_t cond;
	struct wl_list pending;        /* commands waiting to be processed */
	int wake_pipe[2];              /* pipe[0] = read, pipe[1] = write */
	bool shutdown;
	bool wake_enabled;
};

/* -------------------------------------------------------------------------- */
/* Command payload helpers                                                    */
/* -------------------------------------------------------------------------- */

/* Consumer direction for each command type. Input forwarding commands and
 * WINDOW_READY go to the Wayland thread; everything else is a native window
 * operation for app_server. */
static enum koan_queue_direction
direction_for_type(enum koan_cmd_type type) {
	switch (type) {
	case KOAN_CMD_POINTER_MOTION:
	case KOAN_CMD_POINTER_BUTTON:
	case KOAN_CMD_POINTER_AXIS:
	case KOAN_CMD_KEYBOARD_KEY:
	case KOAN_CMD_KEYBOARD_MODIFIERS:
	case KOAN_CMD_KEYBOARD_FOCUS:
	case KOAN_CMD_WINDOW_READY:
		return KOAN_QUEUE_TO_WAYLAND;
	default:
		return KOAN_QUEUE_TO_APP_SERVER;
	}
}

static void *dup_payload(const union koan_cmd_payload *src,
			 enum koan_cmd_type type) {
	union koan_cmd_payload *dst = calloc(1, sizeof(*dst));
	if (!dst)
		return NULL;

	switch (type) {
	case KOAN_CMD_CREATE_WINDOW:
		memcpy(dst, src, sizeof(*dst));
		if (src->create_window.title) {
			dst->create_window.title = strdup(src->create_window.title);
			if (!dst->create_window.title) {
				free(dst);
				return NULL;
			}
		}
		break;
	case KOAN_CMD_SET_TITLE:
		if (src->set_title.title) {
			dst->set_title.title = strdup(src->set_title.title);
			if (!dst->set_title.title) {
				free(dst);
				return NULL;
			}
		}
		dst->set_title.window = src->set_title.window;
		break;
	case KOAN_CMD_SET_BUFFER:
		/* Bitmap data is copied by the caller before pushing; we just
		 * copy the pointer here. The receiver frees the memory. */
		memcpy(dst, src, sizeof(*dst));
		break;
	default:
		memcpy(dst, src, sizeof(*dst));
		break;
	}

	return dst;
}

static void free_payload(union koan_cmd_payload *payload,
			 enum koan_cmd_type type) {
	if (!payload)
		return;

	switch (type) {
	case KOAN_CMD_CREATE_WINDOW:
		free(payload->create_window.title);
		break;
	case KOAN_CMD_SET_TITLE:
		free(payload->set_title.title);
		break;
	case KOAN_CMD_SET_BUFFER:
		free(payload->set_buffer.data);
		break;
	case KOAN_CMD_DESTROY_WINDOW:
	case KOAN_CMD_SET_SIZE:
	case KOAN_CMD_SET_VISIBILITY:
	case KOAN_CMD_WINDOW_READY:
	case KOAN_CMD_POINTER_MOTION:
	case KOAN_CMD_POINTER_BUTTON:
	case KOAN_CMD_POINTER_AXIS:
	case KOAN_CMD_KEYBOARD_KEY:
	case KOAN_CMD_KEYBOARD_MODIFIERS:
	case KOAN_CMD_KEYBOARD_FOCUS:
		/* No heap-owned payload. */
		break;
	}
}

/* -------------------------------------------------------------------------- */
/* Queue API                                                                  */
/* -------------------------------------------------------------------------- */

struct koan_queue *koan_queue_create(void) {
	struct koan_queue *q = calloc(1, sizeof(*q));
	if (!q)
		return NULL;

	if (pthread_mutex_init(&q->mutex, NULL) != 0)
		goto err_mutex;
	if (pthread_cond_init(&q->cond, NULL) != 0)
		goto err_cond;
	wl_list_init(&q->pending);

	if (pipe(q->wake_pipe) != 0)
		goto err_pipe;
	/* Both ends non-blocking: the reader drains with read() until it
	 * would block (a blocking read end hung the Wayland thread forever
	 * after the first wake-up), and writers already tolerate EAGAIN when
	 * the pipe is full of pending wake-ups. */
	for (int i = 0; i < 2; i++) {
		int flags = fcntl(q->wake_pipe[i], F_GETFL);
		if (flags < 0 || fcntl(q->wake_pipe[i], F_SETFL,
				flags | O_NONBLOCK) < 0
				|| fcntl(q->wake_pipe[i], F_SETFD, FD_CLOEXEC) < 0) {
			close(q->wake_pipe[0]);
			close(q->wake_pipe[1]);
			goto err_pipe;
		}
	}
	q->wake_enabled = true;

	return q;

err_pipe:
	pthread_cond_destroy(&q->cond);
err_cond:
	pthread_mutex_destroy(&q->mutex);
err_mutex:
	free(q);
	return NULL;
}

void koan_queue_destroy(struct koan_queue *q) {
	if (!q)
		return;

	pthread_mutex_lock(&q->mutex);

	/* Drop any unprocessed commands, freeing heap-owned payload. */
	struct koan_command *cmd, *tmp;
	wl_list_for_each_safe(cmd, tmp, &q->pending, link) {
		wl_list_remove(&cmd->link);
		free_payload(&cmd->payload, cmd->type);
		free(cmd);
	}

	q->shutdown = true;
	pthread_mutex_unlock(&q->mutex);

	if (q->wake_enabled) {
		close(q->wake_pipe[0]);
		close(q->wake_pipe[1]);
	}
	pthread_cond_destroy(&q->cond);
	pthread_mutex_destroy(&q->mutex);
	free(q);
}

struct koan_command *koan_queue_push(struct koan_queue *q,
				     enum koan_cmd_type type,
				     const union koan_cmd_payload *payload,
				     sem_t *completion_sem) {
	if (!q || !payload)
		return NULL;

	void *dup = dup_payload(payload, type);
	if (!dup)
		return NULL;

	struct koan_command *cmd = calloc(1, sizeof(*cmd));
	if (!cmd) {
		free_payload(dup, type);
		free(dup);
		return NULL;
	}

	cmd->type = type;
	cmd->direction = direction_for_type(type);
	cmd->completion_sem = completion_sem;
	memcpy(&cmd->payload, dup, sizeof(cmd->payload));
	free(dup);

	/* Append at the tail: consumers drain from the head, so commands
	 * (frames, show/hide, destroy) are applied in the order pushed. */
	pthread_mutex_lock(&q->mutex);
	wl_list_insert(q->pending.prev, &cmd->link);
	pthread_mutex_unlock(&q->mutex);

	/* Wake the Wayland thread. */
	if (q->wake_enabled) {
		const uint8_t byte = 0;
		ssize_t n = write(q->wake_pipe[1], &byte, 1);
		if (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK)
			wlr_log(WLR_ERROR, "koan_queue: failed to write wake pipe: %s",
				strerror(errno));
	}

	return cmd;
}

int koan_queue_get_wake_fd(struct koan_queue *q) {
	if (!q || !q->wake_enabled)
		return -1;
	return q->wake_pipe[0];
}

int koan_queue_drain_direction(struct koan_queue *q,
			       enum koan_queue_direction direction,
			       int (*processor)(struct koan_command *cmd,
						void *userdata),
			       void *userdata) {
	if (!q || !processor)
		return -1;

	int processed = 0;

	pthread_mutex_lock(&q->mutex);

	/* Move only this direction's commands to a local list so we can
	 * release the lock while processing (processor may call back into the
	 * queue). Commands for the other consumer stay queued. Insert at the
	 * tail to preserve FIFO order. */
	struct wl_list local;
	wl_list_init(&local);

	struct koan_command *cmd, *tmp;
	wl_list_for_each_safe(cmd, tmp, &q->pending, link) {
		if (cmd->direction != direction)
			continue;

		wl_list_remove(&cmd->link);
		wl_list_insert(local.prev, &cmd->link);
	}

	pthread_mutex_unlock(&q->mutex);

	wl_list_for_each_safe(cmd, tmp, &local, link) {
		wl_list_remove(&cmd->link);
		int rc = processor(cmd, userdata);
		if (rc < 0)
			wlr_log(WLR_ERROR, "koan_queue: command processor returned %d",
				rc);
		free_payload(&cmd->payload, cmd->type);
		free(cmd);
		processed++;
	}

	return processed;
}

int koan_queue_wake(struct koan_queue *q) {
	if (!q || !q->wake_enabled)
		return -1;
	const uint8_t byte = 0;
	ssize_t n = write(q->wake_pipe[1], &byte, 1);
	if (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK)
		return -1;
	return 0;
}

void koan_queue_shutdown(struct koan_queue *q) {
	if (!q)
		return;
	pthread_mutex_lock(&q->mutex);
	q->shutdown = true;
	pthread_mutex_unlock(&q->mutex);
	if (q->wake_enabled) {
		const uint8_t byte = 0;
		(void)write(q->wake_pipe[1], &byte, 1);
	}
}
