/*
 * Koan platform backend for wlroots.
 *
 * Koan provides its own native display and input stack through app_server.
 * This backend exposes Koan displays as wlroots outputs so that Wayland
 * surfaces can be mapped to native Koan windows.
 */

#include <stddef.h>
#include <stdlib.h>
#include <wayland-util.h>
#include <wlr/backend.h>
#include <wlr/backend/interface.h>
#include <wlr/types/wlr_output.h>
#include <wlr/util/log.h>

struct wlr_koan_backend {
	struct wlr_backend backend;
};

static bool koan_backend_start(struct wlr_backend *backend) {
	wlr_log(WLR_INFO, "Koan backend: start");
	return true;
}

static void koan_backend_destroy(struct wlr_backend *backend) {
	wlr_log(WLR_INFO, "Koan backend: destroy");
	struct wlr_koan_backend *kbackend = (struct wlr_koan_backend *)
		((char *)backend - offsetof(struct wlr_koan_backend, backend));
	free(kbackend);
}

/* wlr_backend_init / wlr_backend_finish are normally provided by
 * backend/backend.c, which we do not build for Koan (no DRM/libinput/X11
 * backends). Implement them here for the Koan backend. */
void wlr_backend_init(struct wlr_backend *backend,
		const struct wlr_backend_impl *impl) {
	*backend = (struct wlr_backend){
		.impl = impl,
	};
	wl_signal_init(&backend->events.destroy);
	wl_signal_init(&backend->events.new_input);
	wl_signal_init(&backend->events.new_output);
}

void wlr_backend_finish(struct wlr_backend *backend) {
	wl_signal_emit_mutable(&backend->events.destroy, backend);
}

bool wlr_backend_start(struct wlr_backend *backend) {
	if (backend->impl->start) {
		return backend->impl->start(backend);
	}
	return true;
}

void wlr_backend_destroy(struct wlr_backend *backend) {
	if (!backend) return;
	if (backend->impl && backend->impl->destroy) {
		backend->impl->destroy(backend);
	} else {
		free(backend);
	}
}

int wlr_backend_get_drm_fd(struct wlr_backend *backend) {
	if (!backend->impl->get_drm_fd) return -1;
	return backend->impl->get_drm_fd(backend);
}

bool wlr_backend_test(struct wlr_backend *backend,
		const struct wlr_backend_output_state *states, size_t states_len) {
	if (backend->impl->test) {
		return backend->impl->test(backend, states, states_len);
	}
	return true;
}

bool wlr_backend_commit(struct wlr_backend *backend,
		const struct wlr_backend_output_state *states, size_t states_len) {
	if (backend->impl->commit) {
		return backend->impl->commit(backend, states, states_len);
	}
	return true;
}

static int koan_backend_get_drm_fd(struct wlr_backend *backend) {
	/* Koan does not use DRM directly; app_server owns the display. */
	return -1;
}

static bool koan_backend_test(struct wlr_backend *backend,
		const struct wlr_backend_output_state *states, size_t states_len) {
	return true;
}

static bool koan_backend_commit(struct wlr_backend *backend,
		const struct wlr_backend_output_state *states, size_t states_len) {
	return true;
}

static const struct wlr_backend_impl koan_backend_impl = {
	.start = koan_backend_start,
	.destroy = koan_backend_destroy,
	.get_drm_fd = koan_backend_get_drm_fd,
	.test = koan_backend_test,
	.commit = koan_backend_commit,
};

struct wlr_koan_backend *wlr_koan_backend_create(struct wl_display *display) {
	struct wlr_koan_backend *backend = calloc(1, sizeof(*backend));
	if (!backend) {
		return NULL;
	}

	wlr_backend_init(&backend->backend, &koan_backend_impl);
	/* Koan uses shared-memory (SHM) buffers via the pixman renderer. */
	backend->backend.buffer_caps =
		WLR_BUFFER_CAP_SHM | WLR_BUFFER_CAP_DATA_PTR;
	wlr_log(WLR_INFO, "Koan backend created");
	return backend;
}
