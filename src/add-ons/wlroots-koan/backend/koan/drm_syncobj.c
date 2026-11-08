/*
 * Koan DRM syncobj compatibility implementation.
 *
 * Koan does not use Linux DRM synchronization objects. app_server owns the
 * display and compositor, and wlroots on Koan uses the pixman renderer
 * (CPU-based). This file provides the wlr_drm_syncobj_timeline API using
 * Koan-native primitives (eventfd + counter) so that core wlroots code
 * (scene graph, output, etc.) can compile and run without libdrm.
 *
 * Struct definitions come from <wlr/render/drm_syncobj.h>. We re-purpose
 * the existing fields (drm_fd as eventfd, handle as counter) for Koan.
 */

#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/eventfd.h>
#include <unistd.h>
#include <wayland-server-core.h>
#include <wlr/render/drm_syncobj.h>
#include <wlr/util/addon.h>
#include <wlr/util/log.h>

/* Koan-specific waiter state, attached to the event loop fd data. */
struct koan_syncobj_waiter_data {
	struct wlr_drm_syncobj_timeline_waiter *waiter;
	wlr_drm_syncobj_timeline_ready_callback callback;
};

/* Called when the timeline's eventfd becomes readable via the event loop. */
static int koan_syncobj_eventfd_cb(int fd, uint32_t mask, void *data) {
	struct koan_syncobj_waiter_data *kd = data;
	uint64_t dummy;
	ssize_t n = read(fd, &dummy, sizeof(dummy)); /* consume eventfd */
	if (n < 0)
		return 1;
	if (kd->callback && kd->waiter) {
		kd->callback(kd->waiter);
	}
	return 1;
}

struct wlr_drm_syncobj_timeline *wlr_drm_syncobj_timeline_create(int drm_fd) {
	struct wlr_drm_syncobj_timeline *timeline = calloc(1, sizeof(*timeline));
	if (!timeline) {
		return NULL;
	}

	/* Re-purpose drm_fd as our Koan eventfd for signalling waiters. */
	if (drm_fd >= 0) close(drm_fd);
	timeline->drm_fd = eventfd(0, EFD_SEMAPHORE | EFD_NONBLOCK);
	if (timeline->drm_fd < 0) {
		wlr_log_errno(WLR_ERROR, "eventfd failed");
		free(timeline);
		return NULL;
	}

	timeline->handle = 0;
	wlr_addon_set_init(&timeline->addons);

	wlr_log(WLR_DEBUG, "Koan drm_syncobj: timeline created (event_fd=%d)",
		timeline->drm_fd);
	return timeline;
}

struct wlr_drm_syncobj_timeline *wlr_drm_syncobj_timeline_import(int drm_fd,
		int drm_syncobj_fd) {
	struct wlr_drm_syncobj_timeline *timeline = calloc(1, sizeof(*timeline));
	if (!timeline) {
		close(drm_syncobj_fd);
		return NULL;
	}

	timeline->drm_fd = drm_syncobj_fd;
	timeline->handle = 0;
	wlr_addon_set_init(&timeline->addons);

	wlr_log(WLR_DEBUG, "Koan drm_syncobj: timeline imported (event_fd=%d)",
		timeline->drm_fd);
	return timeline;
}

struct wlr_drm_syncobj_timeline *wlr_drm_syncobj_timeline_ref(
		struct wlr_drm_syncobj_timeline *timeline) {
	if (!timeline) return NULL;
	timeline->n_refs++;  /* WLR_PRIVATE is empty, n_refs is a direct member */
	return timeline;
}

void wlr_drm_syncobj_timeline_unref(struct wlr_drm_syncobj_timeline *timeline) {
	if (!timeline) return;
	if (timeline->n_refs > 0) timeline->n_refs--;
	if (timeline->n_refs > 0) return;

	if (timeline->drm_fd >= 0) close(timeline->drm_fd);
	free(timeline);
}

int wlr_drm_syncobj_timeline_export(struct wlr_drm_syncobj_timeline *timeline) {
	if (!timeline) return -1;
	int fd = dup(timeline->drm_fd);
	if (fd < 0) {
		wlr_log_errno(WLR_ERROR, "dup failed");
	}
	return fd;
}

bool wlr_drm_syncobj_timeline_transfer(struct wlr_drm_syncobj_timeline *dst,
		uint64_t dst_point, struct wlr_drm_syncobj_timeline *src,
		uint64_t src_point) {
	if (!dst || !src) return false;
	if (dst == src) return true;
	if (dst_point > dst->handle) {
		uint64_t val = dst_point - dst->handle;
		for (uint64_t i = 0; i < val; i++) {
			eventfd_write(dst->drm_fd, 1);
		}
		dst->handle = dst_point;
	}
	return true;
}

bool wlr_drm_syncobj_timeline_check(struct wlr_drm_syncobj_timeline *timeline,
		uint64_t point, uint32_t flags, bool *result) {
	if (!timeline || !result) return false;
	*result = (timeline->handle >= point);
	return true;
}

bool wlr_drm_syncobj_timeline_signal(struct wlr_drm_syncobj_timeline *timeline,
		uint64_t point) {
	if (!timeline) return false;
	if (point <= timeline->handle) return true;

	uint64_t val = point - timeline->handle;
	if (eventfd_write(timeline->drm_fd, val) < 0) {
		wlr_log_errno(WLR_ERROR, "eventfd_write failed");
		return false;
	}
	timeline->handle = point;
	return true;
}

bool wlr_drm_syncobj_timeline_waiter_init(
		struct wlr_drm_syncobj_timeline_waiter *waiter,
		struct wlr_drm_syncobj_timeline *timeline, uint64_t point,
		uint32_t flags, struct wl_event_loop *loop,
		wlr_drm_syncobj_timeline_ready_callback callback) {
	if (!waiter || !timeline || !loop || !callback) return false;

	memset(waiter, 0, sizeof(*waiter));

	/* Koan uses synchronous CPU rendering: check immediately. */
	bool signaled;
	if (wlr_drm_syncobj_timeline_check(timeline, point, flags, &signaled) && signaled) {
		/* Already satisfied: create an eventfd to deliver the callback via
		 * the event loop. */
		int ev_fd = eventfd(1, EFD_SEMAPHORE | EFD_NONBLOCK);
		if (ev_fd < 0) {
			wlr_log_errno(WLR_ERROR, "eventfd failed");
			return false;
		}
		struct koan_syncobj_waiter_data *kd = malloc(sizeof(*kd));
		if (!kd) {
			close(ev_fd);
			return false;
		}
		kd->waiter = waiter;
		kd->callback = callback;
		waiter->ev_fd = ev_fd;
		waiter->callback = callback;
		wl_event_loop_add_fd(loop, ev_fd, WL_EVENT_READABLE,
			koan_syncobj_eventfd_cb, kd);
		return true;
	}

	/* Not yet signaled: watch the timeline's eventfd. */
	struct koan_syncobj_waiter_data *kd = malloc(sizeof(*kd));
	if (!kd) {
		return false;
	}
	kd->waiter = waiter;
	kd->callback = callback;
	waiter->ev_fd = timeline->drm_fd;
	waiter->callback = callback;
	wl_event_loop_add_fd(loop, timeline->drm_fd, WL_EVENT_READABLE,
		koan_syncobj_eventfd_cb, kd);
	return true;
}

void wlr_drm_syncobj_timeline_waiter_finish(
		struct wlr_drm_syncobj_timeline_waiter *waiter) {
	/* The waiter is cleaned up when the callback fires. */
	(void)waiter;
}

int wlr_drm_syncobj_timeline_export_sync_file(
		struct wlr_drm_syncobj_timeline *timeline, uint64_t src_point) {
	if (!timeline) return -1;
	int fd = eventfd(1, EFD_SEMAPHORE | EFD_NONBLOCK);
	if (fd < 0) {
		wlr_log_errno(WLR_ERROR, "eventfd failed");
		return -1;
	}
	return fd;
}

bool wlr_drm_syncobj_timeline_import_sync_file(
		struct wlr_drm_syncobj_timeline *timeline, uint64_t dst_point,
		int sync_file_fd) {
	if (!timeline) return false;
	wlr_drm_syncobj_timeline_signal(timeline, dst_point);
	close(sync_file_fd);
	return true;
}

/* ---- drm_syncobj_merger ---- */

struct wlr_drm_syncobj_merger {
	struct wlr_drm_syncobj_timeline *dst_timeline;
	uint64_t dst_point;
	int sync_fd;
	size_t n_ref;
};

struct wlr_drm_syncobj_merger *wlr_drm_syncobj_merger_create(
		struct wlr_drm_syncobj_timeline *dst_timeline, uint64_t dst_point) {
	struct wlr_drm_syncobj_merger *merger = calloc(1, sizeof(*merger));
	if (!merger) return NULL;
	merger->dst_timeline = wlr_drm_syncobj_timeline_ref(dst_timeline);
	merger->dst_point = dst_point;
	merger->sync_fd = -1;
	merger->n_ref = 1;
	return merger;
}

struct wlr_drm_syncobj_merger *wlr_drm_syncobj_merger_ref(
		struct wlr_drm_syncobj_merger *merger) {
	if (!merger) return NULL;
	merger->n_ref++;
	return merger;
}

void wlr_drm_syncobj_merger_unref(struct wlr_drm_syncobj_merger *merger) {
	if (!merger) return;
	merger->n_ref--;
	if (merger->n_ref > 0) return;

	if (merger->sync_fd >= 0) {
		wlr_drm_syncobj_timeline_import_sync_file(merger->dst_timeline,
			merger->dst_point, merger->sync_fd);
		close(merger->sync_fd);
	} else {
		wlr_drm_syncobj_timeline_signal(merger->dst_timeline, merger->dst_point);
	}
	wlr_drm_syncobj_timeline_unref(merger->dst_timeline);
	free(merger);
}

bool wlr_drm_syncobj_merger_add(struct wlr_drm_syncobj_merger *merger,
		struct wlr_drm_syncobj_timeline *src_timeline, uint64_t src_point,
		struct wl_event_loop *loop) {
	if (!merger || !src_timeline || !loop) return false;
	/* Koan: CPU rendering is synchronous, signal immediately. */
	wlr_drm_syncobj_timeline_signal(merger->dst_timeline, merger->dst_point);
	return true;
}
