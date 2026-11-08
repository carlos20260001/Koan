/*
 * Koan window adapter for wlroots.
 *
 * Connects wlroots Wayland surfaces (xdg_toplevel) to native Koan windows
 * managed by app_server. Each Wayland surface becomes a native Koan window.
 *
 * app_server owns the native Window. wlroots-koan owns the Wayland protocol
 * resources and client-side surface state. An explicit relationship is
 * maintained between the wlr_xdg_toplevel and the native window handle so
 * that destroyed Wayland resources never leave dangling native window
 * references, and repeated surface commits never create duplicate windows.
 *
 * Threading model (see koan_queue.h):
 *  - All adapter functions are invoked from the Wayland dispatch thread.
 *  - Callbacks do NOT manipulate native window objects directly. They push
 *    commands onto the thread-safe command queue. app_server's thread drains
 *    the queue and performs the actual native operations.
 *  - Buffer ownership: set_buffer receives Koan-owned memory (a copy of the
 *    client pixels). The wlroots buffer pointer is NEVER handed across the
 *    queue. The receiver copies it into the native bitmap and frees it.
 *
 * Lifecycle (respecting XDG shell semantics):
 *  - surface mapped    -> create native window (async via queue)
 *  - surface committed -> copy client pixels, queue SET_BUFFER
 *  - surface unmapped  -> hide native window (set_visibility false)
 *  - surface destroyed -> destroy native window
 *  - xdg_toplevel close-> close request sent to client (not a destroy)
 */

#include <stdlib.h>
#include <string.h>
#include <drm_fourcc.h>
#include <wlr/types/wlr_buffer.h>
#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_xdg_shell.h>
#include <wlr/util/log.h>

#include "koan_adapter.h"
#include "koan_queue.h"

/* -------------------------------------------------------------------------- */
/* Adapter state                                                              */
/* -------------------------------------------------------------------------- */

struct koan_toplevel {
	struct wl_list link;
	struct wlr_xdg_toplevel *toplevel;
	struct wlr_surface *surface;

	void *native_window;     /* app_server-owned native Window handle */
	bool mapped;
	bool create_pending;     /* CREATE_WINDOW command sent, awaiting result */

	/* Buffered commit data while window creation is pending. The adapter
	 * copies client pixels here on commit; once the native window exists,
	 * the data is queued as SET_BUFFER and freed. */
	bool buffer_pending;
	void *buffer_data;
	int32_t buffer_width;
	int32_t buffer_height;
	int32_t buffer_stride;
	uint32_t buffer_format;

	/* xdg_toplevel role event listeners. */
	struct wl_listener toplevel_destroy;
	struct wl_listener toplevel_request_maximize;
	struct wl_listener toplevel_request_minimize;
	struct wl_listener toplevel_request_fullscreen;
	struct wl_listener toplevel_request_move;
	struct wl_listener toplevel_request_resize;
	struct wl_listener toplevel_set_title;
	struct wl_listener toplevel_set_app_id;
};

static struct wl_list g_toplevels = {0};
static struct koan_queue *g_queue = NULL;
static void *g_userdata = NULL;
static struct koan_window_callbacks g_cbs = {0};

/* -------------------------------------------------------------------------- */
/* Helpers                                                                    */
/* -------------------------------------------------------------------------- */

static struct koan_toplevel *find_toplevel(struct wlr_xdg_toplevel *toplevel) {
	struct koan_toplevel *kt;
	wl_list_for_each(kt, &g_toplevels, link) {
		if (kt->toplevel == toplevel)
			return kt;
	}
	return NULL;
}

static void koan_toplevel_destroy(struct koan_toplevel *kt) {
	if (!kt)
		return;
	wl_list_remove(&kt->toplevel_destroy.link);
	wl_list_remove(&kt->toplevel_request_maximize.link);
	wl_list_remove(&kt->toplevel_request_minimize.link);
	wl_list_remove(&kt->toplevel_request_fullscreen.link);
	wl_list_remove(&kt->toplevel_request_move.link);
	wl_list_remove(&kt->toplevel_request_resize.link);
	wl_list_remove(&kt->toplevel_set_title.link);
	wl_list_remove(&kt->toplevel_set_app_id.link);
	/* Free any buffered commit data that was never delivered. */
	free(kt->buffer_data);
	wl_list_remove(&kt->link);
	free(kt);
}

/* The part of the surface tree app_server should show: the xdg window
 * geometry (clients such as GTK/Firefox draw shadows outside it), clamped
 * to the extents of the whole surface tree, in root-surface coordinates.
 * Falls back to the tree extents. */
static struct wlr_box window_box(struct wlr_xdg_toplevel *toplevel) {
	struct wlr_box ext;
	wlr_surface_get_extents(toplevel->base->surface, &ext);
	struct wlr_box box = ext;
	const struct wlr_box *g = &toplevel->base->geometry;
	if (g->width > 0 && g->height > 0) {
		int32_t x1 = g->x > ext.x ? g->x : ext.x;
		int32_t y1 = g->y > ext.y ? g->y : ext.y;
		int32_t x2 = g->x + g->width;
		int32_t y2 = g->y + g->height;
		if (x2 > ext.x + ext.width)
			x2 = ext.x + ext.width;
		if (y2 > ext.y + ext.height)
			y2 = ext.y + ext.height;
		if (x2 > x1 && y2 > y1) {
			box.x = x1;
			box.y = y1;
			box.width = x2 - x1;
			box.height = y2 - y1;
		}
	}
	return box;
}

struct compose_target {
	uint32_t *pixels;     /* premultiplied ARGB8888, box-sized */
	struct wlr_box box;   /* in root-surface coordinates */
};

/* wlr_surface_for_each_surface() iterator: draw one surface of the tree
 * over what is already composed (root first, children in stacking order).
 * Clients put their content in subsurfaces (Firefox renders the whole
 * window into one, the root only carries the transparent CSD frame), so
 * the root buffer alone is not what the user should see. */
static void compose_surface(struct wlr_surface *surface, int sx, int sy,
			    void *data) {
	struct compose_target *t = data;
	if (!surface->buffer)
		return;

	struct wlr_buffer *buffer = &surface->buffer->base;
	void *src = NULL;
	uint32_t format = 0;
	size_t stride = 0;
	if (!wlr_buffer_begin_data_ptr_access(buffer,
			WLR_BUFFER_DATA_PTR_ACCESS_READ, &src, &format, &stride))
		return;

	bool opaque;
	switch (format) {
	case DRM_FORMAT_ARGB8888:
		opaque = false;
		break;
	case DRM_FORMAT_XRGB8888:
		opaque = true;
		break;
	default:
		/* Every wl_shm client must support these two. */
		wlr_buffer_end_data_ptr_access(buffer);
		wlr_log(WLR_ERROR, "Koan adapter: unsupported shm format 0x%08x",
			format);
		return;
	}

	/* Buffer scale 1 and no viewporter: buffer pixels map 1:1 to surface
	 * coordinates. Clip the buffer against the target box. */
	int32_t x0 = sx < t->box.x ? t->box.x - sx : 0;
	int32_t y0 = sy < t->box.y ? t->box.y - sy : 0;
	int32_t x1 = buffer->width;
	int32_t y1 = buffer->height;
	if (sx + x1 > t->box.x + t->box.width)
		x1 = t->box.x + t->box.width - sx;
	if (sy + y1 > t->box.y + t->box.height)
		y1 = t->box.y + t->box.height - sy;

	for (int32_t y = y0; y < y1; y++) {
		const uint32_t *in = (const uint32_t *)((const uint8_t *)src
			+ (size_t)y * stride);
		uint32_t *out = t->pixels + (size_t)(sy + y - t->box.y)
			* t->box.width + (sx - t->box.x);
		for (int32_t x = x0; x < x1; x++) {
			uint32_t s = in[x];
			uint32_t a = s >> 24;
			if (opaque || a == 0xff) {
				out[x] = s | 0xff000000u;
			} else if (a != 0) {
				/* Premultiplied "over": dst = src + dst * (1 - a). */
				uint32_t d = out[x];
				uint32_t inv = 255 - a;
				uint32_t rb = ((d & 0x00ff00ffu) * inv + 0x00800080u) >> 8;
				uint32_t ag = (((d >> 8) & 0x00ff00ffu) * inv + 0x00800080u)
					>> 8;
				out[x] = s + ((rb & 0x00ff00ffu) | ((ag & 0x00ff00ffu) << 8));
			}
		}
	}

	wlr_buffer_end_data_ptr_access(buffer);
}

/* -------------------------------------------------------------------------- */
/* xdg_toplevel role event handlers (Wayland thread)                          */
/* -------------------------------------------------------------------------- */

static void handle_toplevel_destroy(struct wl_listener *listener, void *data) {
	(void)data;
	struct koan_toplevel *kt =
		wl_container_of(listener, kt, toplevel_destroy);
	koan_adapter_toplevel_destroyed(kt->toplevel);
}

static void handle_toplevel_request_maximize(struct wl_listener *listener,
		void *data) {
	(void)listener;
	(void)data;
	wlr_log(WLR_INFO, "Koan: xdg_toplevel request_maximize (not yet wired)");
}

static void handle_toplevel_request_minimize(struct wl_listener *listener,
		void *data) {
	(void)listener;
	(void)data;
	wlr_log(WLR_INFO, "Koan: xdg_toplevel request_minimize (not yet wired)");
}

static void handle_toplevel_request_fullscreen(struct wl_listener *listener,
		void *data) {
	(void)listener;
	(void)data;
	wlr_log(WLR_INFO, "Koan: xdg_toplevel request_fullscreen (not yet wired)");
}

static void handle_toplevel_request_move(struct wl_listener *listener,
		void *data) {
	(void)listener;
	(void)data;
	wlr_log(WLR_INFO, "Koan: xdg_toplevel request_move (not yet wired)");
}

static void handle_toplevel_request_resize(struct wl_listener *listener,
		void *data) {
	(void)listener;
	(void)data;
	wlr_log(WLR_INFO, "Koan: xdg_toplevel request_resize (not yet wired)");
}

static void handle_toplevel_set_title(struct wl_listener *listener,
		void *data) {
	(void)data;
	struct koan_toplevel *kt =
		wl_container_of(listener, kt, toplevel_set_title);
	const char *title = kt->toplevel->title ? kt->toplevel->title : "";

	if (g_cbs.set_title) {
		g_cbs.set_title(g_userdata, g_queue, kt->native_window, title);
	}
	wlr_log(WLR_INFO, "Koan: xdg_toplevel title set to '%s'", title);
}

static void handle_toplevel_set_app_id(struct wl_listener *listener,
		void *data) {
	(void)data;
	struct koan_toplevel *kt =
		wl_container_of(listener, kt, toplevel_set_app_id);
	const char *app_id = kt->toplevel->app_id ? kt->toplevel->app_id : "";

	wlr_log(WLR_INFO, "Koan: xdg_toplevel app_id set to '%s'", app_id);
}

static void subscribe_toplevel_events(struct koan_toplevel *kt) {
	kt->toplevel_destroy.notify = handle_toplevel_destroy;
	wl_signal_add(&kt->toplevel->events.destroy, &kt->toplevel_destroy);
	kt->toplevel_request_maximize.notify = handle_toplevel_request_maximize;
	wl_signal_add(&kt->toplevel->events.request_maximize,
		&kt->toplevel_request_maximize);
	kt->toplevel_request_fullscreen.notify = handle_toplevel_request_fullscreen;
	wl_signal_add(&kt->toplevel->events.request_fullscreen,
		&kt->toplevel_request_fullscreen);
	kt->toplevel_request_minimize.notify = handle_toplevel_request_minimize;
	wl_signal_add(&kt->toplevel->events.request_minimize,
		&kt->toplevel_request_minimize);
	kt->toplevel_request_move.notify = handle_toplevel_request_move;
	wl_signal_add(&kt->toplevel->events.request_move,
		&kt->toplevel_request_move);
	kt->toplevel_request_resize.notify = handle_toplevel_request_resize;
	wl_signal_add(&kt->toplevel->events.request_resize,
		&kt->toplevel_request_resize);
	kt->toplevel_set_title.notify = handle_toplevel_set_title;
	wl_signal_add(&kt->toplevel->events.set_title, &kt->toplevel_set_title);
	kt->toplevel_set_app_id.notify = handle_toplevel_set_app_id;
	wl_signal_add(&kt->toplevel->events.set_app_id, &kt->toplevel_set_app_id);
}

/* -------------------------------------------------------------------------- */
/* Command queue processing (Wayland thread)                                  */
/* -------------------------------------------------------------------------- */

/* Process app_server -> Wayland commands drained from the queue. */
static int process_app_server_command(struct koan_command *cmd, void *userdata) {
	(void)userdata;

	switch (cmd->type) {
	case KOAN_CMD_WINDOW_READY:
		/* app_server notifies that a native window is ready for a
		 * toplevel whose creation was previously pending. */
		{
			struct wlr_xdg_toplevel *toplevel =
				cmd->payload.window_ready.toplevel;
			void *window = cmd->payload.window_ready.window;
			struct koan_toplevel *kt = find_toplevel(toplevel);
			if (kt) {
				kt->native_window = window;
				kt->create_pending = false;

				/* The toplevel may have been re-mapped while window
				 * creation was pending; apply its visibility now. */
				if (kt->mapped && g_cbs.set_visibility) {
					g_cbs.set_visibility(g_userdata, g_queue,
						kt->native_window, true);
				}

				/* Deliver any buffered commit. */
				if (kt->buffer_pending && kt->native_window &&
				    g_cbs.set_buffer) {
					g_cbs.set_buffer(g_userdata, g_queue,
						kt->native_window,
						kt->buffer_data,
						kt->buffer_width,
						kt->buffer_height,
						kt->buffer_stride,
						kt->buffer_format);
					/* set_buffer takes ownership of the data. */
					kt->buffer_data = NULL;
					kt->buffer_pending = false;
				}
			}
		}
		break;

	case KOAN_CMD_TOPLEVEL_CLOSE:
		{
			/* The toplevel may be gone by now: only use it if it is
			 * still one of ours. */
			struct koan_toplevel *kt =
				find_toplevel(cmd->payload.toplevel_close.toplevel);
			if (kt)
				wlr_xdg_toplevel_send_close(kt->toplevel);
		}
		break;

	case KOAN_CMD_TOPLEVEL_CONFIGURE:
		{
			const struct koan_cmd_toplevel_configure *c =
				&cmd->payload.toplevel_configure;
			struct koan_toplevel *kt = find_toplevel(c->toplevel);
			if (!kt || !kt->toplevel->base->initialized)
				break;
			if (c->width > 0 && c->height > 0)
				wlr_xdg_toplevel_set_size(kt->toplevel, c->width, c->height);
			if (c->maximized >= 0)
				wlr_xdg_toplevel_set_maximized(kt->toplevel, c->maximized != 0);
			if (c->activated >= 0)
				wlr_xdg_toplevel_set_activated(kt->toplevel, c->activated != 0);
		}
		break;

	default:
		/* Wayland -> app_server commands (CREATE_WINDOW, SET_BUFFER, etc.)
		 * are pushed by the Wayland thread itself and processed by
		 * app_server, not here. */
		break;
	}

	return 0;
}

/* -------------------------------------------------------------------------- */
/* Public adapter API                                                         */
/* -------------------------------------------------------------------------- */

void koan_adapter_set_window_callbacks(
		const struct koan_window_callbacks *cb,
		struct koan_queue *queue, void *userdata) {
	if (cb)
		g_cbs = *cb;
	g_queue = queue;
	g_userdata = userdata;
}

void koan_adapter_init(struct wlr_xdg_shell *xdg_shell,
		       struct koan_queue *queue, void *userdata) {
	(void)xdg_shell; /* new_toplevel listener is registered in koan_server.c */
	wl_list_init(&g_toplevels);
	g_queue = queue;
	/* app_server registers its bridge as callback userdata before the
	 * server is created. Preserve it; only fall back to the server pointer
	 * when no window callback userdata was supplied. */
	if (g_userdata == NULL)
		g_userdata = userdata;
	wlr_log(WLR_INFO, "Koan adapter: initialised");
}

int koan_adapter_get_wake_fd(void) {
	if (!g_queue)
		return -1;
	return koan_queue_get_wake_fd(g_queue);
}

struct koan_queue *koan_adapter_queue(void) {
	return g_queue;
}

int koan_adapter_drain_commands(void) {
	if (!g_queue)
		return -1;
	return koan_queue_drain_direction(g_queue, KOAN_QUEUE_TO_WAYLAND,
		process_app_server_command, NULL);
}

/* Create a native window for a mapped xdg_toplevel.
 * Invokes the app_server create_window callback, which queues
 * KOAN_CMD_CREATE_WINDOW and wakes app_server. The native window handle is
 * delivered asynchronously via KOAN_CMD_WINDOW_READY. */
static void create_native_window(struct koan_toplevel *kt,
				 int32_t width, int32_t height) {
	if (!g_queue || !g_cbs.create_window || kt->create_pending)
		return;

	kt->create_pending = true;

	g_cbs.create_window(g_userdata, g_queue, kt->toplevel, width, height);
}

/* Deliver the toplevel's current content to app_server: the whole surface
 * tree composed into its window geometry, in Koan-owned memory. If the
 * native window doesn't exist yet (creation pending), keep the data for
 * later delivery. */
static void deliver_buffer(struct koan_toplevel *kt) {
	struct wlr_surface *surface = kt->surface;
	if (!surface || !surface->buffer)
		return;

	struct compose_target t = { NULL, window_box(kt->toplevel) };
	if (t.box.width <= 0 || t.box.height <= 0)
		return;
	if ((int64_t)t.box.width * t.box.height * 4 > INT32_MAX) {
		wlr_log(WLR_ERROR, "Koan adapter: window too large (%dx%d)",
			t.box.width, t.box.height);
		return;
	}
	t.pixels = calloc((size_t)t.box.width * t.box.height, sizeof(uint32_t));
	if (!t.pixels) {
		wlr_log(WLR_ERROR, "Koan adapter: failed to allocate %dx%d window",
			t.box.width, t.box.height);
		return;
	}
	wlr_surface_for_each_surface(surface, compose_surface, &t);

	void *data = t.pixels;
	int32_t width = t.box.width;
	int32_t height = t.box.height;
	int32_t stride = width * 4;
	uint32_t format = DRM_FORMAT_ARGB8888;

	if (kt->native_window && g_cbs.set_buffer) {
		/* Window exists: queue the buffer immediately. */
		g_cbs.set_buffer(g_userdata, g_queue, kt->native_window,
			data, width, height, stride, format);
		/* set_buffer takes ownership of data. */
	} else {
		/* Window not yet created: buffer the data for later. */
		free(kt->buffer_data);
		kt->buffer_data = data;
		kt->buffer_width = width;
		kt->buffer_height = height;
		kt->buffer_stride = stride;
		kt->buffer_format = format;
		kt->buffer_pending = true;
	}
}

/* Size for a newly mapped toplevel's native window: its window geometry
 * within the surface tree (a toplevel only maps with a buffer). The
 * configured size is 0x0 because Koan lets clients pick their size. */
static void initial_size(struct koan_toplevel *kt,
			 int32_t *width, int32_t *height) {
	struct wlr_box box = window_box(kt->toplevel);
	if (box.width <= 0 || box.height <= 0) {
		*width = 640;
		*height = 480;
		return;
	}
	*width = box.width;
	*height = box.height;
}

/* Called by the server when a new xdg_toplevel is created. Subscribes to
 * role events and records the toplevel for lifecycle tracking. */
void koan_adapter_handle_new_toplevel(struct wlr_xdg_toplevel *toplevel) {
	if (!toplevel)
		return;
	if (find_toplevel(toplevel))
		return;

	struct koan_toplevel *kt = calloc(1, sizeof(*kt));
	if (!kt)
		return;
	kt->toplevel = toplevel;
	kt->surface = toplevel->base->surface;

	subscribe_toplevel_events(kt);

	wl_list_insert(&g_toplevels, &kt->link);

	wlr_log(WLR_INFO, "Koan adapter: new xdg_toplevel (%s)",
		toplevel->title ? toplevel->title : "(null)");
}

/* Called by the server when a toplevel is mapped (ready to display). */
void koan_adapter_toplevel_mapped(struct wlr_xdg_toplevel *toplevel) {
	if (!toplevel)
		return;

	struct koan_toplevel *kt = find_toplevel(toplevel);
	if (kt != NULL) {
		/* Re-map: ensure the window exists and is visible again. */
		kt->mapped = true;
		if (kt->native_window == NULL && !kt->create_pending) {
			int32_t width, height;
			initial_size(kt, &width, &height);
			create_native_window(kt, width, height);
		} else if (kt->native_window != NULL && g_cbs.set_visibility) {
			g_cbs.set_visibility(g_userdata, g_queue,
				kt->native_window, true);
		}
		return;
	}

	/* Should not happen: handle_new_toplevel should have been called first.
	 * Create the record now. */
	kt = calloc(1, sizeof(*kt));
	if (!kt)
		return;
	kt->toplevel = toplevel;
	kt->surface = toplevel->base->surface;
	subscribe_toplevel_events(kt);
	kt->mapped = true;

	wl_list_insert(&g_toplevels, &kt->link);

	int32_t width, height;
	initial_size(kt, &width, &height);

	create_native_window(kt, width, height);

	wlr_log(WLR_INFO, "Koan adapter: toplevel mapped (%s)",
		toplevel->title ? toplevel->title : "(null)");
}

/* Called by the server when a toplevel is unmapped. */
void koan_adapter_toplevel_unmapped(struct wlr_xdg_toplevel *toplevel) {
	if (!toplevel)
		return;
	struct koan_toplevel *kt = find_toplevel(toplevel);
	if (kt == NULL)
		return;

	/* XDG semantics: unmap means hide, not destroy. The window remains
	 * associated with the toplevel and can be re-mapped later. */
	if (kt->native_window && g_cbs.set_visibility) {
		g_cbs.set_visibility(g_userdata, g_queue, kt->native_window, false);
	}
	kt->mapped = false;

	wlr_log(WLR_INFO, "Koan adapter: toplevel unmapped (%s)",
		toplevel->title ? toplevel->title : "(null)");
}

/* Called by the server when a toplevel surface commits new content. */
void koan_adapter_toplevel_committed(struct wlr_xdg_toplevel *toplevel) {
	if (!toplevel)
		return;
	struct koan_toplevel *kt = find_toplevel(toplevel);
	if (kt == NULL)
		return;

	deliver_buffer(kt);
}

/* Called by the server when a toplevel is destroyed. */
void koan_adapter_toplevel_destroyed(struct wlr_xdg_toplevel *toplevel) {
	if (!toplevel)
		return;
	struct koan_toplevel *kt = find_toplevel(toplevel);
	if (kt == NULL)
		return;

	if (kt->native_window && g_cbs.destroy_window) {
		g_cbs.destroy_window(g_userdata, g_queue, kt->native_window);
	}
	koan_toplevel_destroy(kt);

	wlr_log(WLR_INFO, "Koan adapter: toplevel destroyed (%s)",
		toplevel->title ? toplevel->title : "(null)");
}
