/*
 * SPDX-License-Identifier: MIT
 * Copyright © 2026 carlos20260001
 */

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

#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <drm_fourcc.h>
#include <wlr/types/wlr_buffer.h>
#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_xdg_shell.h>
#include <wlr/util/log.h>

#include "koan_adapter.h"
#include "koan_frame.h"
#include "koan_test.h"
#include "koan_queue.h"
#include "koan_server.h"

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
	/* A state request made while create_pending, forwarded to the window
	 * manager on WINDOW_READY: 1 enter, 0 leave, -1 none. */
	int32_t deferred_maximized;
	int32_t deferred_fullscreen;

	/* Buffered commit data while window creation is pending. The adapter
	 * copies client pixels here on commit; once the native window exists,
	 * the data is queued as SET_BUFFER and freed. */
	bool buffer_pending;
	void *buffer_data;
	int32_t buffer_width;
	int32_t buffer_height;
	int32_t buffer_stride;
	uint32_t buffer_format;
	float buffer_scale;
	int32_t buffer_acked_width;
	int32_t buffer_acked_height;

	/* The tree's layout when it was last composed (tree_signature()), and
	 * whether it has been composed at all: a commit that changes neither
	 * that nor any pixel is not composed again. */
	bool composed;
	uint64_t composed_signature;

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

/* Toplevels destroyed while app_server was still creating their native
 * window (create_pending): the window's WINDOW_READY is still on its way
 * and the window must be destroyed when it arrives. Only the address is
 * kept, as a key. A new toplevel can reuse the address, but app_server
 * answers creations in order, so the oldest orphan with the address owns
 * the first WINDOW_READY for it. */
struct koan_orphan {
	struct wl_list link;          /* g_orphans, oldest first */
	void *key;                    /* the dead toplevel or popup */
};
static struct wl_list g_orphans = {0};

/* An xdg_popup shown as a native window (koan_window_callbacks.
 * create_popup). Created on its first mapped commit, destroyed when it is
 * unmapped or destroyed; a remap makes a new one. */
struct koan_popup {
	struct wl_list link;                /* g_popups */
	struct wlr_xdg_popup *popup;
	struct wlr_xdg_toplevel *toplevel;  /* the root toplevel */
	void *native_window;
	bool create_pending;
	bool failed;                        /* app_server had no window for it */
	int32_t x, y;                       /* last position sent */
	void *buffer_data;                  /* frame waiting for POPUP_READY */
	int32_t buffer_width, buffer_height, buffer_stride;
	float buffer_scale;
	struct wl_listener destroy;
	struct wl_listener unmap;
};
static struct wl_list g_popups = {0};

static void remember_orphan(void *key);
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
	koan_frame_release(kt->buffer_data);
	wl_list_remove(&kt->link);
	free(kt);
}

/* The part of the surface tree app_server should show: the xdg window
 * geometry (clients such as GTK/Firefox draw shadows outside it), clamped
 * to the extents of the whole surface tree, in root-surface coordinates.
 * Falls back to the tree extents. */
/* The part of an xdg_surface's tree a native window shows: its window
 * geometry clamped to the tree's extents (surface coordinates). */
static struct wlr_box surface_box(struct wlr_xdg_surface *xdg_surface) {
	struct wlr_box ext;
	wlr_surface_get_extents(xdg_surface->surface, &ext);
	struct wlr_box box = ext;
	const struct wlr_box *g = &xdg_surface->geometry;
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

static struct wlr_box window_box(struct wlr_xdg_toplevel *toplevel) {
	return surface_box(toplevel->base);
}

struct wlr_box koan_adapter_window_box(struct wlr_xdg_toplevel *toplevel) {
	return window_box(toplevel);
}

struct compose_target {
	uint32_t *pixels;     /* premultiplied ARGB8888, box * scale pixels */
	struct wlr_box box;   /* in root-surface coordinates */
	float scale;          /* pixels per surface coordinate unit */
	int32_t width;        /* box.width * scale, rounded */
	int32_t height;       /* box.height * scale, rounded */
};

/* Largest composed window, in pixels. A client sizes its surfaces
 * independently of its buffers (wp_viewport.set_destination, subsurface
 * offsets), so a 4 x 4 buffer can claim any box, and each commit makes
 * app_server allocate and import a frame of that size. 16384 is the usual
 * GPU texture limit; the area allows a window across two 8K screens
 * (15360 x 4320) with room to spare, at 512 MiB. */
#define KOAN_MAX_COMPOSE_SIDE 16384
#define KOAN_MAX_COMPOSE_PIXELS (16384.0 * 8192.0)

/* Allocate the target's pixels for its box at the output's scale. Returns
 * false (nothing allocated) when the box is empty or too large. */
static bool compose_target_init(struct compose_target *t,
		struct wlr_box box) {
	t->pixels = NULL;
	t->box = box;
	t->scale = koan_server_output_scale_exact();
	if (box.width <= 0 || box.height <= 0)
		return false;
	/* In double: box * scale can exceed int32 before it is checked. */
	const double width = round((double)box.width * t->scale);
	const double height = round((double)box.height * t->scale);
	if (width > KOAN_MAX_COMPOSE_SIDE || height > KOAN_MAX_COMPOSE_SIDE
			|| width * height > KOAN_MAX_COMPOSE_PIXELS) {
		wlr_log(WLR_ERROR, "Koan adapter: window too large (%dx%d at "
			"scale %.2f), not shown", box.width, box.height, t->scale);
		return false;
	}
	t->width = (int32_t)width;
	t->height = (int32_t)height;
	/* Not cleared: compose_tree() writes every pixel. */
	t->pixels = koan_frame_alloc((size_t)t->width * t->height
		* sizeof(uint32_t));
	if (!t->pixels) {
		wlr_log(WLR_ERROR, "Koan adapter: failed to allocate %dx%d window",
			t->width, t->height);
		return false;
	}
	return true;
}

/* Premultiplied "over": dst = src + dst * (1 - a). */
static inline uint32_t blend_pixel(uint32_t s, uint32_t d, bool opaque) {
	uint32_t a = s >> 24;
	if (opaque || a == 0xff)
		return s | 0xff000000u;
	if (a == 0)
		return d;
	uint32_t inv = 255 - a;
	uint32_t rb = ((d & 0x00ff00ffu) * inv + 0x00800080u) >> 8;
	uint32_t ag = (((d >> 8) & 0x00ff00ffu) * inv + 0x00800080u) >> 8;
	return s + ((rb & 0x00ff00ffu) | ((ag & 0x00ff00ffu) << 8));
}

/* The premultiplied ARGB pixel at (fx, fy) in buffer pixels, filtered
 * from the four around it (weights in 1/256); outside the buffer the edge
 * pixels repeat. Clients that render at another density than the window
 * is composed at (the integer wl_output.scale, or 1) are resampled this
 * way: nearest neighbour dropped whole rows and columns of a 2x buffer at
 * 150% and left text with uneven strokes. */
static inline uint32_t sample_bilinear(const uint8_t *src, size_t stride,
		int32_t width, int32_t height, double fx, double fy) {
	fx -= 0.5;
	fy -= 0.5;
	int32_t x0 = (int32_t)floor(fx);
	int32_t y0 = (int32_t)floor(fy);
	uint32_t wx = (uint32_t)((fx - x0) * 256.0);
	uint32_t wy = (uint32_t)((fy - y0) * 256.0);
	int32_t x1 = x0 + 1, y1 = y0 + 1;
	if (x0 < 0) x0 = 0;
	if (y0 < 0) y0 = 0;
	if (x1 > width - 1) x1 = width - 1;
	if (y1 > height - 1) y1 = height - 1;
	if (x0 > width - 1) x0 = width - 1;
	if (y0 > height - 1) y0 = height - 1;
	const uint32_t *r0 = (const uint32_t *)(src + (size_t)y0 * stride);
	const uint32_t *r1 = (const uint32_t *)(src + (size_t)y1 * stride);
	const uint32_t p[4] = { r0[x0], r0[x1], r1[x0], r1[x1] };
	const uint32_t w[4] = {
		(256 - wx) * (256 - wy), wx * (256 - wy),
		(256 - wx) * wy, wx * wy };
	uint32_t out = 0;
	for (int shift = 0; shift < 32; shift += 8) {
		uint32_t sum = 0;
		for (int i = 0; i < 4; i++)
			sum += ((p[i] >> shift) & 0xff) * w[i];
		out |= ((sum + 32768) >> 16) << shift;
	}
	return out;
}

/* One surface of a tree being composed, at (sx, sy) in root-surface
 * coordinates. */
struct compose_item {
	struct wlr_surface *surface;
	int sx, sy;
};

struct compose_items {
	struct compose_item *items;
	size_t count, capacity;
	bool failed;
};

/* wlr_surface_for_each_surface() iterator: list the surfaces with a
 * buffer, root first, children in stacking order. */
static void compose_collect(struct wlr_surface *surface, int sx, int sy,
		void *data) {
	struct compose_items *list = data;
	if (!surface->buffer || list->failed)
		return;
	if (list->count == list->capacity) {
		size_t capacity = list->capacity ? list->capacity * 2 : 8;
		struct compose_item *items = realloc(list->items,
			capacity * sizeof(*items));
		if (!items) {
			list->failed = true;
			return;
		}
		list->items = items;
		list->capacity = capacity;
	}
	list->items[list->count++] = (struct compose_item){ surface, sx, sy };
}

/* The target pixels a surface covers: current.width x current.height
 * surface units at (sx, sy) are target->scale times as many pixels. */
static void compose_item_rect(const struct compose_target *t,
		const struct compose_item *item, int32_t *ox, int32_t *oy,
		int32_t *dw, int32_t *dh) {
	const float scale = t->scale;
	*dw = (int32_t)lroundf(item->surface->current.width * scale);
	*dh = (int32_t)lroundf(item->surface->current.height * scale);
	*ox = (int32_t)lroundf((item->sx - t->box.x) * scale);
	*oy = (int32_t)lroundf((item->sy - t->box.y) * scale);
}

/* Add the target pixels the surface's opaque region fully covers to
 * covered. A pixel counts only when it lies wholly inside the region, so
 * at a fractional scale the edges round inwards and a partly covered
 * pixel is still composed from the surfaces below. */
static void compose_add_opaque(const struct compose_target *t,
		const struct compose_item *item, pixman_region32_t *covered) {
	int32_t ox, oy, dw, dh;
	compose_item_rect(t, item, &ox, &oy, &dw, &dh);
	const double scale = t->scale;
	int nrects = 0;
	const pixman_box32_t *rects = pixman_region32_rectangles(
		&item->surface->opaque_region, &nrects);
	for (int i = 0; i < nrects; i++) {
		int32_t x1 = (int32_t)ceil(rects[i].x1 * scale);
		int32_t y1 = (int32_t)ceil(rects[i].y1 * scale);
		int32_t x2 = (int32_t)floor(rects[i].x2 * scale);
		int32_t y2 = (int32_t)floor(rects[i].y2 * scale);
		if (x2 > dw)
			x2 = dw;
		if (y2 > dh)
			y2 = dh;
		if (x1 < x2 && y1 < y2)
			pixman_region32_union_rect(covered, covered, ox + x1, oy + y1,
				(unsigned)(x2 - x1), (unsigned)(y2 - y1));
	}
}

/* Draw the parts of one surface that lie in write and blend (target
 * pixels): write replaces the target's pixels (the surface is opaque
 * there), blend draws over what is already composed.
 *
 * Its buffer (or the viewport's source crop of it) is mapped onto the
 * surface's target pixels (compose_item_rect()): 1:1 when the client
 * rendered at the target's scale (a wp_fractional_scale_v1 client, or
 * buffer_scale == target scale), otherwise filtered (sample_bilinear()).
 * Buffer transforms are not applied. */
static void compose_draw(struct compose_target *t,
		const struct compose_item *item, pixman_region32_t *write,
		pixman_region32_t *blend) {
	struct wlr_surface *surface = item->surface;
	int32_t ox, oy, dw, dh;
	compose_item_rect(t, item, &ox, &oy, &dw, &dh);
	if (dw <= 0 || dh <= 0)
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

	/* Source rectangle in buffer pixels. */
	double srcX = 0, srcY = 0;
	double srcW = buffer->width, srcH = buffer->height;
	if (surface->current.viewport.has_src) {
		srcX = surface->current.viewport.src.x * surface->current.scale;
		srcY = surface->current.viewport.src.y * surface->current.scale;
		srcW = surface->current.viewport.src.width * surface->current.scale;
		srcH = surface->current.viewport.src.height
			* surface->current.scale;
	}
	/* 1:1 when the buffer has the composed size: a client that rendered at
	 * the target's scale. wp_fractional_scale_v1 leaves rounding the
	 * scaled size to the client (Firefox truncates 106.75 to 106 where the
	 * target rounds to 107), so one pixel either way is still a copy; the
	 * missing row or column repeats the buffer's last. */
	bool direct = !surface->current.viewport.has_src
		&& abs(buffer->width - dw) <= 1 && abs(buffer->height - dh) <= 1;
	const double stepX = srcW / dw;
	const double stepY = srcH / dh;
	const int32_t lastX = buffer->width - 1;
	/* XRGB has no alpha: its pixels are written opaque. */
	const uint32_t alphaFill = opaque ? 0xff000000u : 0;

	/* write: where the surface declares itself opaque, so nothing under it
	 * shows and its pixels replace the target's (which are not cleared
	 * there, koan_frame.h). blend: over what is composed below. */
	for (int pass = 0; pass < 2; pass++) {
		const bool replace = pass == 0;
		int nrects = 0;
		const pixman_box32_t *rects = pixman_region32_rectangles(
			replace ? write : blend, &nrects);
		for (int r = 0; r < nrects; r++) {
			/* The rectangle in the surface's own target pixels. */
			const int32_t x0 = rects[r].x1 - ox, x1 = rects[r].x2 - ox;
			const int32_t y0 = rects[r].y1 - oy, y1 = rects[r].y2 - oy;
			for (int32_t y = y0; y < y1; y++) {
				uint32_t *out = t->pixels + (size_t)(oy + y) * t->width + ox;
				if (direct) {
					const int32_t by = y < buffer->height
						? y : buffer->height - 1;
					const uint32_t *in = (const uint32_t *)
						((const uint8_t *)src + (size_t)by * stride);
					if (replace) {
						/* The bulk of a full-screen video frame: a row
						 * copy the compiler vectorizes. */
						const int32_t end = x1 < lastX ? x1 : lastX;
						int32_t x = x0;
						for (; x < end; x++)
							out[x] = in[x] | alphaFill;
						for (; x < x1; x++)
							out[x] = in[lastX] | alphaFill;
						continue;
					}
					for (int32_t x = x0; x < x1; x++)
						out[x] = blend_pixel(in[x < lastX ? x : lastX], out[x],
							opaque);
					continue;
				}
				const double fy = srcY + (y + 0.5) * stepY;
				for (int32_t x = x0; x < x1; x++) {
					uint32_t pixel = sample_bilinear((const uint8_t *)src,
						stride, buffer->width, buffer->height,
						srcX + (x + 0.5) * stepX, fy);
					out[x] = replace ? pixel | alphaFill
						: blend_pixel(pixel, out[x], opaque);
				}
			}
		}
	}

	wlr_buffer_end_data_ptr_access(buffer);
}

/* Compose a surface tree into the target: every surface over the ones
 * below it, root first. Clients put their content in subsurfaces (Firefox
 * renders the whole window into one, the root only carries GTK's CSD
 * frame), so the root buffer alone is not what the user should see.
 *
 * What a surface declares opaque (wl_surface.set_opaque_region, or an
 * XRGB buffer) hides the surfaces below, so those parts of them are not
 * composed: Firefox's root is drawn at the integer scale and would be
 * resampled on every frame of a video, only to be covered by the page. */
static void compose_tree(struct compose_target *t,
		struct wlr_surface *root) {
	struct compose_items list = {0};
	wlr_surface_for_each_surface(root, compose_collect, &list);
	/* draw[0..count) and write[0..count) in one allocation. */
	pixman_region32_t *draw = list.failed || list.count == 0 ? NULL
		: calloc(list.count * 2, sizeof(*draw));
	if (!draw) {
		/* Nothing to draw (or no memory to plan it): a transparent frame,
		 * since the frame's memory is reused, not cleared. */
		memset(t->pixels, 0, (size_t)t->width * t->height * sizeof(uint32_t));
		free(list.items);
		return;
	}

	/* Top down: each surface draws what the ones above leave visible;
	 * where it is opaque itself it replaces the pixels (write), elsewhere
	 * it blends over the surfaces below (draw minus write). */
	pixman_region32_t covered, own;
	pixman_region32_init(&covered);
	pixman_region32_init(&own);
	pixman_region32_t *write = draw + list.count;
	for (size_t i = list.count; i-- > 0;) {
		int32_t ox, oy, dw, dh;
		compose_item_rect(t, &list.items[i], &ox, &oy, &dw, &dh);
		pixman_region32_init(&draw[i]);
		pixman_region32_init(&write[i]);
		if (dw > 0 && dh > 0) {
			pixman_region32_union_rect(&draw[i], &draw[i], ox, oy,
				(unsigned)dw, (unsigned)dh);
			pixman_region32_intersect_rect(&draw[i], &draw[i], 0, 0,
				(unsigned)t->width, (unsigned)t->height);
			pixman_region32_subtract(&draw[i], &draw[i], &covered);
		}
		pixman_region32_clear(&own);
		compose_add_opaque(t, &list.items[i], &own);
		pixman_region32_intersect(&write[i], &draw[i], &own);
		pixman_region32_subtract(&draw[i], &draw[i], &write[i]);
		pixman_region32_union(&covered, &covered, &own);
	}
	pixman_region32_fini(&own);

	/* The frame's memory is reused, not cleared: clear what no surface
	 * writes, the transparent background the others blend over. */
	pixman_region32_t clear;
	pixman_region32_init_rect(&clear, 0, 0, (unsigned)t->width,
		(unsigned)t->height);
	pixman_region32_subtract(&clear, &clear, &covered);
	pixman_region32_fini(&covered);
	int nrects = 0;
	const pixman_box32_t *rects = pixman_region32_rectangles(&clear, &nrects);
	for (int r = 0; r < nrects; r++) {
		for (int32_t y = rects[r].y1; y < rects[r].y2; y++)
			memset(t->pixels + (size_t)y * t->width + rects[r].x1, 0,
				(size_t)(rects[r].x2 - rects[r].x1) * sizeof(uint32_t));
	}
	pixman_region32_fini(&clear);

	for (size_t i = 0; i < list.count; i++) {
		if (pixman_region32_not_empty(&draw[i])
				|| pixman_region32_not_empty(&write[i]))
			compose_draw(t, &list.items[i], &write[i], &draw[i]);
		pixman_region32_fini(&draw[i]);
		pixman_region32_fini(&write[i]);
	}
	free(draw);
	free(list.items);
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

/* xdg-shell.xml: after set_/unset_maximized and set_/unset_fullscreen
 * "the compositor will respond by emitting a configure event". The window
 * manager (app_server: zoom / fullscreen of the native window) decides and
 * answers with KOAN_CMD_TOPLEVEL_CONFIGURE. Without one (no request_state
 * callback, or no native window yet) reply with the current state so the
 * client stops waiting. Covered by tests/koan_protocol_test.c (xdg:
 * toplevel-*-granted). */
static void request_state(struct koan_toplevel *kt, int32_t maximized,
		int32_t fullscreen, const char *request) {
	if (kt->native_window && g_cbs.request_state) {
		g_cbs.request_state(g_userdata, g_queue, kt->native_window,
			maximized, fullscreen);
		return;
	}
	/* app_server is creating the native window: the window manager
	 * decides once it exists (WINDOW_READY). Covered by
	 * tests/koan_protocol_test.c (xdg: toplevel-maximize-before-window-
	 * ready). */
	if (kt->create_pending && g_cbs.request_state) {
		if (maximized >= 0)
			kt->deferred_maximized = maximized;
		if (fullscreen >= 0)
			kt->deferred_fullscreen = fullscreen;
		return;
	}
	if (kt->toplevel->base->initialized)
		wlr_xdg_surface_schedule_configure(kt->toplevel->base);
	wlr_log(WLR_INFO, "Koan: xdg_toplevel %s answered with the current "
		"state (no window manager for it)", request);
}

static void handle_toplevel_request_maximize(struct wl_listener *listener,
		void *data) {
	(void)data;
	struct koan_toplevel *kt =
		wl_container_of(listener, kt, toplevel_request_maximize);
	request_state(kt, kt->toplevel->requested.maximized ? 1 : 0, -1,
		"request_maximize");
}

static void handle_toplevel_request_minimize(struct wl_listener *listener,
		void *data) {
	(void)data;
	struct koan_toplevel *kt =
		wl_container_of(listener, kt, toplevel_request_minimize);
	/* xdg-shell has no reply to set_minimized and no unminimize request:
	 * the window manager hides the native window like its minimize
	 * button. Covered by tests/koan_protocol_test.c (xdg:
	 * toplevel-set-minimized). */
	if (kt->native_window && g_cbs.request_minimize) {
		g_cbs.request_minimize(g_userdata, g_queue, kt->native_window);
		return;
	}
	wlr_log(WLR_INFO, "Koan: xdg_toplevel request_minimize ignored (no "
		"window manager for it)");
}

static void handle_toplevel_request_fullscreen(struct wl_listener *listener,
		void *data) {
	(void)data;
	struct koan_toplevel *kt =
		wl_container_of(listener, kt, toplevel_request_fullscreen);
	request_state(kt, -1, kt->toplevel->requested.fullscreen ? 1 : 0,
		"request_fullscreen");
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
/* ---- orphans ----------------------------------------------------------- */

static void remember_orphan(void *key) {
	struct koan_orphan *orphan = calloc(1, sizeof(*orphan));
	if (orphan == NULL) {
		wlr_log(WLR_ERROR, "Koan adapter: out of memory; the native window "
			"of a destroyed surface will leak");
		return;
	}
	orphan->key = key;
	wl_list_insert(g_orphans.prev, &orphan->link);
}

/* A WINDOW_READY / POPUP_READY for a surface that died while its window
 * was being created: destroy the window. Returns true if it was one. */
static bool take_orphan(void *key, void *window) {
	struct koan_orphan *orphan;
	wl_list_for_each(orphan, &g_orphans, link) {
		if (orphan->key == key) {
			wl_list_remove(&orphan->link);
			free(orphan);
			if (window && g_cbs.destroy_window)
				g_cbs.destroy_window(g_userdata, g_queue, window);
			return true;
		}
	}
	return false;
}

/* ---- popups ------------------------------------------------------------ */

static struct koan_popup *find_popup(struct wlr_xdg_popup *popup) {
	struct koan_popup *kp;
	wl_list_for_each(kp, &g_popups, link) {
		if (kp->popup == popup)
			return kp;
	}
	return NULL;
}

static struct wlr_xdg_toplevel *popup_toplevel(struct wlr_xdg_popup *popup) {
	struct wlr_surface *parent = popup->parent;
	struct wlr_xdg_popup *p;
	while (parent != NULL
			&& (p = wlr_xdg_popup_try_from_wlr_surface(parent)) != NULL)
		parent = p->parent;
	return parent != NULL ? wlr_xdg_toplevel_try_from_wlr_surface(parent)
		: NULL;
}

/* The popup's box (popup surface coordinates) and where its top-left is in
 * the toplevel's window-content coordinates. */
static struct wlr_box popup_box(struct koan_popup *kp, int32_t *x,
		int32_t *y) {
	struct wlr_box box = surface_box(kp->popup->base);
	const struct wlr_box *g = &kp->popup->base->geometry;
	/* get_toplevel_coords starts at the popup's parent: the popup's own
	 * position (current.geometry, relative to the parent's window
	 * geometry) is the caller's part, as in sway and tinywl. */
	int tx, ty;
	wlr_xdg_popup_get_toplevel_coords(kp->popup,
		kp->popup->current.geometry.x + box.x - g->x,
		kp->popup->current.geometry.y + box.y - g->y, &tx, &ty);
	struct wlr_box content = window_box(kp->toplevel);
	*x = tx - content.x;
	*y = ty - content.y;
	return box;
}

/* The native window goes away (unmap, destroy, or its toplevel lost
 * focus); a pending one is destroyed when POPUP_READY comes. */
static void popup_hide(struct koan_popup *kp) {
	if (kp->native_window && g_cbs.destroy_window)
		g_cbs.destroy_window(g_userdata, g_queue, kp->native_window);
	else if (kp->create_pending)
		remember_orphan(kp->popup);
	kp->native_window = NULL;
	kp->create_pending = false;
	kp->failed = false;
	koan_frame_release(kp->buffer_data);
	kp->buffer_data = NULL;
}

static void popup_ready(struct wlr_xdg_popup *popup, void *window) {
	if (take_orphan(popup, window))
		return;
	struct koan_popup *kp = find_popup(popup);
	if (kp == NULL || !kp->create_pending) {
		if (window && g_cbs.destroy_window)
			g_cbs.destroy_window(g_userdata, g_queue, window);
		return;
	}
	kp->create_pending = false;
	kp->native_window = window;
	kp->failed = window == NULL;
	if (window && kp->buffer_data && g_cbs.set_buffer) {
		g_cbs.set_buffer(g_userdata, g_queue, window, kp->buffer_data,
			kp->buffer_width, kp->buffer_height, kp->buffer_stride,
			DRM_FORMAT_ARGB8888, kp->buffer_scale, 0, 0);
		kp->buffer_data = NULL; /* set_buffer takes it */
	}
	koan_frame_release(kp->buffer_data);
	kp->buffer_data = NULL;
}

static void handle_popup_unmap(struct wl_listener *listener, void *data) {
	(void)data;
	struct koan_popup *kp = wl_container_of(listener, kp, unmap);
	koan_server_popup_keyboard(kp->popup, false);
	popup_hide(kp);
}

static void handle_popup_destroy(struct wl_listener *listener, void *data) {
	(void)data;
	struct koan_popup *kp = wl_container_of(listener, kp, destroy);
	popup_hide(kp);
	wl_list_remove(&kp->destroy.link);
	wl_list_remove(&kp->unmap.link);
	wl_list_remove(&kp->link);
	free(kp);
}

void koan_adapter_popup_new(struct wlr_xdg_popup *popup) {
	if (popup == NULL || find_popup(popup) != NULL)
		return;
	struct koan_popup *kp = calloc(1, sizeof(*kp));
	if (kp == NULL)
		return;
	kp->popup = popup;
	kp->destroy.notify = handle_popup_destroy;
	wl_signal_add(&popup->events.destroy, &kp->destroy);
	kp->unmap.notify = handle_popup_unmap;
	wl_signal_add(&popup->base->surface->events.unmap, &kp->unmap);
	wl_list_insert(&g_popups, &kp->link);
}

/* Compose the popup's tree and show it. Covered by
 * tests/koan_protocol_test.c (xdg: popup-*). */
void koan_adapter_popup_committed(struct wlr_xdg_popup *popup) {
	struct koan_popup *kp = find_popup(popup);
	if (kp == NULL || !g_cbs.create_popup || kp->failed)
		return;
	if (kp->toplevel == NULL)
		kp->toplevel = popup_toplevel(popup);
	if (kp->toplevel == NULL || find_toplevel(kp->toplevel) == NULL)
		return; /* a popup of a layer surface or of nothing we show */

	int32_t x, y;
	struct compose_target t;
	if (!compose_target_init(&t, popup_box(kp, &x, &y)))
		return;
	compose_tree(&t, popup->base->surface);
	int32_t stride = t.width * 4;

	if (kp->native_window) {
		if ((x != kp->x || y != kp->y) && g_cbs.move_popup)
			g_cbs.move_popup(g_userdata, g_queue, kp->native_window, x, y);
		kp->x = x;
		kp->y = y;
		g_cbs.set_buffer(g_userdata, g_queue, kp->native_window, t.pixels,
			t.width, t.height, stride, DRM_FORMAT_ARGB8888, t.scale, 0, 0);
		return;
	}
	/* Keep the newest frame until the window exists. */
	koan_frame_release(kp->buffer_data);
	kp->buffer_data = t.pixels;
	kp->buffer_width = t.width;
	kp->buffer_height = t.height;
	kp->buffer_stride = stride;
	kp->buffer_scale = t.scale;
	if (!kp->create_pending) {
		kp->create_pending = true;
		kp->x = x;
		kp->y = y;
		g_cbs.create_popup(g_userdata, g_queue, popup, kp->toplevel, x, y,
			t.box.width, t.box.height);
	}
}

void koan_adapter_forget_popups(void) {
	if (g_popups.next == NULL)
		return;
	while (!wl_list_empty(&g_popups)) {
		struct koan_popup *kp = wl_container_of(g_popups.next, kp, link);
		wl_list_remove(&kp->destroy.link);
		wl_list_remove(&kp->unmap.link);
		wl_list_remove(&kp->link);
		koan_frame_release(kp->buffer_data);
		free(kp);
	}
}

/* xdg-shell (xdg_popup.popup_done): the compositor dismisses popups, e.g.
 * when the user activates another window. A deactivated toplevel takes
 * its popups along, as native menus close when their window loses focus.
 * toplevel NULL with grabbed_only: a click outside every client surface
 * ends the grabs (xdg_popup.grab), so menus close, tooltips stay. */
static void dismiss_popups(struct wlr_xdg_toplevel *toplevel,
		bool grabbed_only) {
	struct koan_popup *kp, *tmp;
	bool again = true;
	/* Destroying a popup destroys its children (and their records), so
	 * restart after each one. */
	while (again) {
		again = false;
		wl_list_for_each_safe(kp, tmp, &g_popups, link) {
			struct wlr_xdg_popup *parent =
				wlr_xdg_popup_try_from_wlr_surface(kp->popup->parent);
			bool outermost = grabbed_only
				? kp->popup->seat != NULL
					&& (parent == NULL || parent->seat == NULL)
				: parent == NULL;
			if ((toplevel == NULL || popup_toplevel(kp->popup) == toplevel)
					&& outermost) {
				wlr_xdg_popup_destroy(kp->popup);
				again = true;
				break;
			}
		}
	}
}

static int process_app_server_command(struct koan_command *cmd, void *userdata) {
	(void)userdata;

	switch (cmd->type) {
	case KOAN_CMD_POPUP_READY:
		popup_ready(cmd->payload.popup_ready.popup,
			cmd->payload.popup_ready.window);
		break;

	case KOAN_CMD_DISMISS_POPUPS:
		dismiss_popups(NULL, true);
		break;

	case KOAN_CMD_WINDOW_READY:
		/* app_server notifies that a native window is ready for a
		 * toplevel whose creation was previously pending. */
		{
			struct wlr_xdg_toplevel *toplevel =
				cmd->payload.window_ready.toplevel;
			void *window = cmd->payload.window_ready.window;
			if (take_orphan(toplevel, window))
				break;
			struct koan_toplevel *kt = find_toplevel(toplevel);
			if (kt) {
				kt->native_window = window;
				kt->create_pending = false;

				if ((kt->deferred_maximized >= 0
						|| kt->deferred_fullscreen >= 0)
						&& g_cbs.request_state) {
					g_cbs.request_state(g_userdata, g_queue,
						kt->native_window, kt->deferred_maximized,
						kt->deferred_fullscreen);
				}
				kt->deferred_maximized = -1;
				kt->deferred_fullscreen = -1;

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
						kt->buffer_format,
						kt->buffer_scale,
						kt->buffer_acked_width,
						kt->buffer_acked_height);
					/* set_buffer takes ownership of the data. */
					kt->buffer_data = NULL;
					kt->buffer_pending = false;
				}
			}
		}
		break;

	/* Input from the native window. find_toplevel() keeps a stale key
	 * (window gone meanwhile) from reaching wlroots. */
	case KOAN_CMD_POINTER_MOTION:
		{
			const struct koan_cmd_pointer_motion *m =
				&cmd->payload.pointer_motion;
			struct koan_toplevel *kt = find_toplevel(m->toplevel);
			if (!kt || !kt->mapped)
				break;
			if (m->leave)
				koan_server_pointer_leave(kt->toplevel);
			else
				koan_server_pointer_motion(kt->toplevel, m->time, m->x, m->y);
		}
		break;

	case KOAN_CMD_POINTER_BUTTON:
		{
			const struct koan_cmd_pointer_button *b =
				&cmd->payload.pointer_button;
			struct koan_toplevel *kt = find_toplevel(b->toplevel);
			if (kt && kt->mapped)
				koan_server_pointer_button(kt->toplevel, b->time, b->button,
					b->pressed != 0);
		}
		break;

	case KOAN_CMD_POINTER_AXIS:
		{
			const struct koan_cmd_pointer_axis *a = &cmd->payload.pointer_axis;
			struct koan_toplevel *kt = find_toplevel(a->toplevel);
			if (kt && kt->mapped)
				koan_server_pointer_axis(kt->toplevel, a->time,
					a->horizontal != 0, a->steps);
		}
		break;

	case KOAN_CMD_KEYBOARD_KEY:
		{
			const struct koan_cmd_keyboard_key *k = &cmd->payload.keyboard_key;
			struct koan_toplevel *kt = find_toplevel(k->toplevel);
			if (kt && kt->mapped)
				koan_server_keyboard_key(kt->toplevel, k->time, k->key,
					k->pressed != 0);
		}
		break;

	case KOAN_CMD_KEYBOARD_FOCUS:
		{
			const struct koan_cmd_keyboard_focus *f =
				&cmd->payload.keyboard_focus;
			struct koan_toplevel *kt = find_toplevel(f->toplevel);
			if (kt && kt->mapped)
				koan_server_keyboard_focus(kt->toplevel, f->focused != 0);
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
			if (c->fullscreen != KOAN_STATE_KEEP)
				wlr_xdg_toplevel_set_fullscreen(kt->toplevel,
					c->fullscreen == KOAN_STATE_ENTER);
			/* The answer to a state request needs a configure even when
			 * the window manager kept the state (wlroots only schedules
			 * one for a change). */
			if (c->maximized >= 0 || c->fullscreen != KOAN_STATE_KEEP)
				wlr_xdg_surface_schedule_configure(kt->toplevel->base);
			if (c->activated >= 0)
				wlr_xdg_toplevel_set_activated(kt->toplevel, c->activated != 0);
			if (c->activated == 0)
				dismiss_popups(kt->toplevel, false);
		}
		break;

	case KOAN_CMD_TEST_FENCE_BEGIN:
	case KOAN_CMD_TEST_FENCE_PART:
		koan_test_handle_command(cmd);
		break;

	case KOAN_CMD_OUTPUT_MODE:
		koan_server_set_output_dimensions(cmd->payload.output_mode.width,
			cmd->payload.output_mode.height,
			cmd->payload.output_mode.refresh_mhz,
			cmd->payload.output_mode.scale);
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
	/* Orphans of a previous server in this process can never be answered
	 * (its queue is gone). */
	if (g_orphans.next != NULL) {
		while (!wl_list_empty(&g_orphans)) {
			struct koan_orphan *o =
				wl_container_of(g_orphans.next, o, link);
			wl_list_remove(&o->link);
			free(o);
		}
	}
	wl_list_init(&g_orphans);
	wl_list_init(&g_popups);
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

void koan_adapter_wake_app_server(void) {
	if (g_cbs.wake_app_server)
		g_cbs.wake_app_server(g_userdata);
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

/* FNV-1a over the bytes of one value. */
static void signature_add(uint64_t *hash, const void *data, size_t size) {
	const uint8_t *bytes = data;
	for (size_t i = 0; i < size; i++) {
		*hash ^= bytes[i];
		*hash *= 0x100000001b3ull;
	}
}

static void signature_add_surface(struct wlr_surface *surface, int sx,
		int sy, void *data) {
	uint64_t *hash = data;
	const struct wlr_surface_state *state = &surface->current;
	const bool has_buffer = surface->buffer != NULL;
	signature_add(hash, &surface, sizeof(surface));
	signature_add(hash, &sx, sizeof(sx));
	signature_add(hash, &sy, sizeof(sy));
	signature_add(hash, &has_buffer, sizeof(has_buffer));
	signature_add(hash, &state->width, sizeof(state->width));
	signature_add(hash, &state->height, sizeof(state->height));
	signature_add(hash, &state->buffer_width, sizeof(state->buffer_width));
	signature_add(hash, &state->buffer_height, sizeof(state->buffer_height));
	signature_add(hash, &state->scale, sizeof(state->scale));
	signature_add(hash, &state->viewport.has_src,
		sizeof(state->viewport.has_src));
	if (state->viewport.has_src)
		signature_add(hash, &state->viewport.src,
			sizeof(state->viewport.src));
}

/* What composing the tree depends on besides its pixels: the window box,
 * the output scale and, for each surface in stacking order, where it is,
 * its size, its buffer's size and its viewport source. These change
 * without buffer damage (a subsurface moved, a new window geometry, the
 * screen's scale), so they are compared rather than tracked. */
static uint64_t tree_signature(struct koan_toplevel *kt) {
	uint64_t hash = 0xcbf29ce484222325ull;
	struct wlr_box box = window_box(kt->toplevel);
	float scale = koan_server_output_scale_exact();
	signature_add(&hash, &box, sizeof(box));
	signature_add(&hash, &scale, sizeof(scale));
	wlr_surface_for_each_surface(kt->surface, signature_add_surface, &hash);
	return hash;
}

/* Deliver the toplevel's current content to app_server: the whole surface
 * tree composed into its window geometry, in Koan-owned memory. If the
 * native window doesn't exist yet (creation pending), keep the data for
 * later delivery. */
static void deliver_buffer(struct koan_toplevel *kt) {
	struct wlr_surface *surface = kt->surface;
	if (!surface || !surface->buffer)
		return;

	struct compose_target t;
	if (!compose_target_init(&t, window_box(kt->toplevel)))
		return;
	compose_tree(&t, surface);

	void *data = t.pixels;
	int32_t width = t.width;
	int32_t height = t.height;
	int32_t stride = width * 4;
	uint32_t format = DRM_FORMAT_ARGB8888;

	/* The configure this frame answers: wlroots applies the acked
	 * configure's size to toplevel->current with the commit. */
	const int32_t ackedWidth = kt->toplevel->current.width;
	const int32_t ackedHeight = kt->toplevel->current.height;

	if (kt->native_window && g_cbs.set_buffer) {
		/* Window exists: queue the buffer immediately. */
		g_cbs.set_buffer(g_userdata, g_queue, kt->native_window,
			data, width, height, stride, format, t.scale, ackedWidth,
			ackedHeight);
		/* set_buffer takes ownership of data. */
	} else {
		/* Window not yet created: buffer the data for later. */
		koan_frame_release(kt->buffer_data);
		kt->buffer_data = data;
		kt->buffer_width = width;
		kt->buffer_height = height;
		kt->buffer_stride = stride;
		kt->buffer_format = format;
		kt->buffer_scale = t.scale;
		kt->buffer_acked_width = ackedWidth;
		kt->buffer_acked_height = ackedHeight;
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
	kt->deferred_maximized = -1;
	kt->deferred_fullscreen = -1;
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
	kt->deferred_maximized = -1;
	kt->deferred_fullscreen = -1;
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
	kt->composed = false; /* the next map is composed whatever it damages */

	wlr_log(WLR_INFO, "Koan adapter: toplevel unmapped (%s)",
		toplevel->title ? toplevel->title : "(null)");
}

/* Called by the server when a surface of the toplevel's tree commits. */
void koan_adapter_toplevel_committed(struct wlr_xdg_toplevel *toplevel,
		bool damaged) {
	if (!toplevel)
		return;
	struct koan_toplevel *kt = find_toplevel(toplevel);
	if (kt == NULL || !kt->surface || !kt->surface->buffer)
		return;

	/* Clients commit without new pixels too (Firefox answers every frame
	 * callback with a commit): compose only what changed. */
	uint64_t signature = tree_signature(kt);
	if (kt->composed && !damaged && signature == kt->composed_signature)
		return;
	kt->composed = true;
	kt->composed_signature = signature;
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
	} else if (kt->create_pending) {
		/* Covered by tests/koan_protocol_test.c (xdg:
		 * toplevel-destroyed-before-window-ready). */
		remember_orphan(toplevel);
	}
	koan_toplevel_destroy(kt);

	wlr_log(WLR_INFO, "Koan adapter: toplevel destroyed (%s)",
		toplevel->title ? toplevel->title : "(null)");
}
