/*
 * Copyright 2026, Koan contributors
 * SPDX-License-Identifier: Apache-2.0
 *
 * Part of Koan, a VitruvianOS fork
 */


/*! Minimal Wayland server running inside app_server.

    Provides wl_display with core globals so Wayland clients can connect.
    Each Wayland surface maps to a Vitruvian Window via Desktop.

    This file is only compiled when VOS_WAYLAND is defined (i.e.
    wayland-server and wayland-client development packages are installed).
 */


#ifdef VOS_WAYLAND

#include "WaylandServer.h"

static bool
_is_debug_user()
{
	const char* user = getenv("USER");
	if (user != NULL && strcmp(user, "vos-live") == 0)
		return true;
	if (getuid() == 995)
		return true;
	return false;
}

#define WLD_DEBUG(...) do { if (_is_debug_user()) { fprintf(stderr, "[WL-DEBUG] "); fprintf(stderr, __VA_ARGS__); fprintf(stderr, "\n"); fflush(stderr); } } while(0)


#include <new>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/stat.h>
#include <math.h>
#include <pwd.h>
#include <time.h>
#include <sys/types.h>
#include <sys/time.h>

#include <Locker.h>
#include <Message.h>
#include <String.h>
#include <Looper.h>

#include <wayland-server.h>
#include <wayland-server-core.h>
#include <wayland-util.h>

#if __has_include("xdg-shell-server-protocol.h")
#include "xdg-shell-server-protocol.h"
#else
#include <xdg-shell-server-protocol.h>
#endif
#include "Desktop.h"
#include "DrawingEngine.h"
#include "ServerBitmap.h"
#include "WaylandResourceUtils.h"
#include "WaylandDataDeviceManager.h"

struct PoolData {
	WaylandServer*	server;
	void*			data;
	int32			fd;
	int32			size;
	int32			refs;
	BObjectList<WaylandSurface, false> buffers;
};

static void _PoolDataUnref(PoolData* pool);
static void _AttachedBufferDestroyed(struct wl_listener* listener, void* data);


// Protocol handler forward declarations. The interface tables below are
// defined before the bind/create functions, so every handler used by a
// table must be declared first.
static void _compositor_destroy(struct wl_resource* resource);
static void _compositor_create_surface(struct wl_client* client,
	struct wl_resource* resource, uint32_t id);
static void _compositor_create_region(struct wl_client* client,
	struct wl_resource* resource, uint32_t id);
static void _output_bind(struct wl_client* client,
	void* data, uint32_t version, uint32_t id);
static void _output_release(struct wl_client* client,
	struct wl_resource* resource);

static void _SurfaceDestroy(struct wl_resource* resource);
static void _SurfaceReleaseAttachedBuffer(WaylandSurface* surface,
	bool sendRelease);
static void _SurfaceAttach(struct wl_client* client,
	struct wl_resource* resource, struct wl_resource* buffer,
	int32_t x, int32_t y);
static void _SurfaceDamage(struct wl_client* client,
	struct wl_resource* resource, int32_t x, int32_t y,
	int32_t width, int32_t height);
static void _SurfaceFrame(struct wl_client* client,
	struct wl_resource* resource, uint32_t callback);
static void _SurfaceSetOpaqueRegion(struct wl_client* client,
	struct wl_resource* resource, struct wl_resource* region);
static void _SurfaceSetInputRegion(struct wl_client* client,
	struct wl_resource* resource, struct wl_resource* region);
static void _SurfaceCommit(struct wl_client* client,
	struct wl_resource* resource);
static void _SurfaceSetBufferTransform(struct wl_client* client,
	struct wl_resource* resource, int32_t transform);
static void _SurfaceSetBufferScale(struct wl_client* client,
	struct wl_resource* resource, int32_t scale);
static void _SurfaceDamageBuffer(struct wl_client* client,
	struct wl_resource* resource, int32_t x, int32_t y,
	int32_t width, int32_t height);
static void _SurfaceOffset(struct wl_client* client,
	struct wl_resource* resource, int32_t x, int32_t y);

static void _RegionDestroy(struct wl_resource* resource);
static void _RegionAdd(struct wl_client* client, struct wl_resource* resource,
	int32_t x, int32_t y, int32_t width, int32_t height);
static void _RegionSubtract(struct wl_client* client,
	struct wl_resource* resource, int32_t x, int32_t y,
	int32_t width, int32_t height);

static void _ShmDestroy(struct wl_resource* resource);
static void _ShmCreatePool(struct wl_client* client,
	struct wl_resource* resource, uint32_t id, int32_t fd, int32_t size);
static void _ShmRelease(struct wl_client* client,
	struct wl_resource* resource);
static void _ShmPoolCreateBuffer(struct wl_client* client,
	struct wl_resource* resource, uint32_t id, int32_t offset,
	int32_t width, int32_t height, int32_t stride, uint32_t format);
static void _ShmPoolResize(struct wl_client* client,
	struct wl_resource* resource, int32_t size);

static void _BufferDestroy(struct wl_resource* resource);
static void _BufferRelease(struct wl_client* client,
	struct wl_resource* resource);

static void _SeatDestroy(struct wl_resource* resource);
static void _SeatGetPointer(struct wl_client* client,
	struct wl_resource* resource, uint32_t id);
static void _SeatGetKeyboard(struct wl_client* client,
	struct wl_resource* resource, uint32_t id);
static void _SeatGetTouch(struct wl_client* client,
	struct wl_resource* resource, uint32_t id);
static void _SeatRelease(struct wl_client* client,
	struct wl_resource* resource);
static void _SurfaceDestroyRequest(struct wl_client* client,
	struct wl_resource* resource);
static void _RegionDestroyRequest(struct wl_client* client,
	struct wl_resource* resource);
static void _ShmPoolDestroyRequest(struct wl_client* client,
	struct wl_resource* resource);
static void _XdgWmBaseDestroyRequest(struct wl_client* client,
	struct wl_resource* resource);
static void _XdgPositionerDestroyRequest(struct wl_client* client,
	struct wl_resource* resource);
static void _XdgSurfaceDestroyRequest(struct wl_client* client,
	struct wl_resource* resource);
static void _XdgToplevelDestroyRequest(struct wl_client* client,
	struct wl_resource* resource);
static void _XdgPopupDestroyRequest(struct wl_client* client,
	struct wl_resource* resource);

static void _PointerDestroy(struct wl_resource* resource);
static void _PointerSetCursor(struct wl_client* client,
	struct wl_resource* resource, uint32_t serial,
	struct wl_resource* surface, int32_t hotspot_x, int32_t hotspot_y);
static void _PointerRelease(struct wl_client* client,
	struct wl_resource* resource);

static void _KeyboardDestroy(struct wl_resource* resource);
static void _KeyboardRelease(struct wl_client* client,
	struct wl_resource* resource);
static void _TouchRelease(struct wl_client* client,
	struct wl_resource* resource);

static void _XdgWmBaseDestroy(struct wl_resource* resource);
static void _XdgWmBaseCreatePositioner(struct wl_client* client,
	struct wl_resource* resource, uint32_t id);
static void _XdgWmBaseGetXdgSurface(struct wl_client* client,
	struct wl_resource* resource, uint32_t id,
	struct wl_resource* surface);
static void _XdgWmBasePong(struct wl_client* client,
	struct wl_resource* resource, uint32_t serial);

static void _XdgPositionerDestroy(struct wl_resource* resource);
static void _XdgPositionerSetSize(struct wl_client* client,
	struct wl_resource* resource, int32_t width, int32_t height);
static void _XdgPositionerSetAnchorRect(struct wl_client* client,
	struct wl_resource* resource, int32_t x, int32_t y,
	int32_t width, int32_t height);
static void _XdgPositionerSetAnchor(struct wl_client* client,
	struct wl_resource* resource, uint32_t anchor);
static void _XdgPositionerSetGravity(struct wl_client* client,
	struct wl_resource* resource, uint32_t gravity);
static void _XdgPositionerSetConstraintAdjustment(struct wl_client* client,
	struct wl_resource* resource, uint32_t constraint_adjustment);
static void _XdgPositionerSetOffset(struct wl_client* client,
	struct wl_resource* resource, int32_t x, int32_t y);
static void _XdgPositionerSetReactive(struct wl_client* client,
	struct wl_resource* resource);
static void _XdgPositionerSetParentSize(struct wl_client* client,
	struct wl_resource* resource, int32_t parent_width,
	int32_t parent_height);
static void _XdgPositionerSetParentConfigure(struct wl_client* client,
	struct wl_resource* resource, uint32_t serial);

static void _XdgSurfaceDestroy(struct wl_resource* resource);
static void _XdgSurfaceGetToplevel(struct wl_client* client,
	struct wl_resource* resource, uint32_t id);
static void _XdgSurfaceGetPopup(struct wl_client* client,
	struct wl_resource* resource, uint32_t id,
	struct wl_resource* parent, struct wl_resource* positioner);
static void _XdgSurfaceSetWindowGeometry(struct wl_client* client,
	struct wl_resource* resource, int32_t x, int32_t y,
	int32_t width, int32_t height);
static void _XdgSurfaceAckConfigure(struct wl_client* client,
	struct wl_resource* resource, uint32_t serial);

static void _XdgToplevelDestroy(struct wl_resource* resource);
static void _XdgToplevelSetParent(struct wl_client* client,
	struct wl_resource* resource, struct wl_resource* parent);
static void _XdgToplevelSetTitle(struct wl_client* client,
	struct wl_resource* resource, const char* title);
static void _XdgToplevelSetAppId(struct wl_client* client,
	struct wl_resource* resource, const char* app_id);
static void _XdgToplevelShowWindowMenu(struct wl_client* client,
	struct wl_resource* resource, struct wl_resource* seat,
	uint32_t serial, int32_t x, int32_t y);
static void _XdgToplevelMove(struct wl_client* client,
	struct wl_resource* resource, struct wl_resource* seat,
	uint32_t serial);
static void _XdgToplevelResize(struct wl_client* client,
	struct wl_resource* resource, struct wl_resource* seat,
	uint32_t serial, uint32_t edges);
static void _XdgToplevelSetMaxSize(struct wl_client* client,
	struct wl_resource* resource, int32_t width, int32_t height);
static void _XdgToplevelSetMinSize(struct wl_client* client,
	struct wl_resource* resource, int32_t width, int32_t height);
static void _XdgToplevelSetMaximized(struct wl_client* client,
	struct wl_resource* resource);
static void _XdgToplevelUnsetMaximized(struct wl_client* client,
	struct wl_resource* resource);
static void _XdgToplevelSetFullscreen(struct wl_client* client,
	struct wl_resource* resource, struct wl_resource* output);
static void _XdgToplevelUnsetFullscreen(struct wl_client* client,
	struct wl_resource* resource);
static void _XdgToplevelSetMinimized(struct wl_client* client,
	struct wl_resource* resource);

static void _XdgPopupDestroy(struct wl_resource* resource);
static void _XdgPopupGrab(struct wl_client* client,
	struct wl_resource* resource, struct wl_resource* seat,
	uint32_t serial);
static void _XdgPopupReposition(struct wl_client* client,
	struct wl_resource* resource, struct wl_resource* positioner,
	uint32_t token);


// ---------------------------------------------------------------------------
// Interface tables
// ---------------------------------------------------------------------------

static const struct wl_compositor_interface sCompositorInterface = {
	_compositor_create_surface,
	_compositor_create_region
};

static const struct wl_surface_interface sSurfaceInterface = {
	_SurfaceDestroyRequest,
	_SurfaceAttach,
	_SurfaceDamage,
	_SurfaceFrame,
	_SurfaceSetOpaqueRegion,
	_SurfaceSetInputRegion,
	_SurfaceCommit,
	_SurfaceSetBufferTransform,
	_SurfaceSetBufferScale,
	_SurfaceDamageBuffer,
	_SurfaceOffset
};

static const struct wl_region_interface sRegionInterface = {
	_RegionDestroyRequest,
	_RegionAdd,
	_RegionSubtract
};

static const struct wl_shm_interface sShmInterface = {
	_ShmCreatePool,
	_ShmRelease
};

static const struct wl_shm_pool_interface sShmPoolInterface = {
	_ShmPoolCreateBuffer,
	_ShmPoolDestroyRequest,
	_ShmPoolResize
};

static const struct wl_buffer_interface sBufferInterface = {
	_BufferRelease
};

static const struct wl_seat_interface sSeatInterface = {
	_SeatGetPointer,
	_SeatGetKeyboard,
	_SeatGetTouch,
	_SeatRelease
};

static const struct wl_pointer_interface sPointerInterface = {
	_PointerSetCursor,
	_PointerRelease
};

static const struct wl_keyboard_interface sKeyboardInterface = {
	_KeyboardRelease
};

static const struct wl_touch_interface sTouchInterface = {
	_TouchRelease
};

static const struct xdg_wm_base_interface sXdgWmBaseInterface = {
	_XdgWmBaseDestroyRequest,
	_XdgWmBaseCreatePositioner,
	_XdgWmBaseGetXdgSurface,
	_XdgWmBasePong
};

static const struct xdg_positioner_interface sXdgPositionerInterface = {
	_XdgPositionerDestroyRequest,
	_XdgPositionerSetSize,
	_XdgPositionerSetAnchorRect,
	_XdgPositionerSetAnchor,
	_XdgPositionerSetGravity,
	_XdgPositionerSetConstraintAdjustment,
	_XdgPositionerSetOffset,
	_XdgPositionerSetReactive,
	_XdgPositionerSetParentSize,
	_XdgPositionerSetParentConfigure
};

static const struct xdg_surface_interface sXdgSurfaceInterface = {
	_XdgSurfaceDestroyRequest,
	_XdgSurfaceGetToplevel,
	_XdgSurfaceGetPopup,
	_XdgSurfaceSetWindowGeometry,
	_XdgSurfaceAckConfigure
};

static const struct xdg_toplevel_interface sXdgToplevelInterface = {
	_XdgToplevelDestroyRequest,
	_XdgToplevelSetParent,
	_XdgToplevelSetTitle,
	_XdgToplevelSetAppId,
	_XdgToplevelShowWindowMenu,
	_XdgToplevelMove,
	_XdgToplevelResize,
	_XdgToplevelSetMaxSize,
	_XdgToplevelSetMinSize,
	_XdgToplevelSetMaximized,
	_XdgToplevelUnsetMaximized,
	_XdgToplevelSetFullscreen,
	_XdgToplevelUnsetFullscreen,
	_XdgToplevelSetMinimized
};

static const struct xdg_popup_interface sXdgPopupInterface = {
	_XdgPopupDestroyRequest,
	_XdgPopupGrab,
	_XdgPopupReposition
};


static void
_compositor_destroy(struct wl_resource* resource)
{
	(void)resource;
}


// ---------------------------------------------------------------------------
// Wayland surface <-> color space mapping
// ---------------------------------------------------------------------------

static color_space
_wayland_format_to_colorspace(uint32_t format)
{
	// Use named WL_SHM_FORMAT_* constants; these are defined in
	// wayland-client-protocol.h and map to the following byte layouts
	// on a little-endian host:
	//   ARGB8888: bytes B,G,R,A  → B_RGBA32
	//   XRGB8888: bytes B,G,R,X  → B_RGB32
	//   BGRA8888: bytes A,R,G,B  → no exact BeOS match; use B_RGBA32 + note
	//   BGRX8888: bytes X,R,G,B  → B_RGB32
	switch (format) {
		case WL_SHM_FORMAT_ARGB8888:
			return B_RGBA32;    // BGRA byte order on LE, identical to B_RGBA32
		case WL_SHM_FORMAT_XRGB8888:
			return B_RGB32;     // BGRX byte order on LE, identical to B_RGB32
		case WL_SHM_FORMAT_BGRA8888:
			// ARGB byte order on LE; closest BeOS space is B_RGBA32_BIG but
			// channel swaps must be handled by the paint path.  Map to B_RGBA32
			// for now; a future blit path should swizzle R↔B before drawing.
			return B_RGBA32;
		case WL_SHM_FORMAT_BGRX8888:
			return B_RGB32;
		default:
			return B_RGBA32;
	}
}


// ---------------------------------------------------------------------------
// WaylandSurface
// ---------------------------------------------------------------------------

WaylandSurface::WaylandSurface(struct wl_resource* resource)
	:
	wlSurface(resource),
	xdgSurface(NULL),
	attachedBuffer(NULL),
	attachedBufferSurface(NULL),
	server(NULL),
	pool(NULL),
	referenceCount(1),

	bitmapData(NULL),
	bitmapSize(0),
	bitmapCapacity(0),
	bitmapOffset(0),
	bitmapWidth(0),
	bitmapHeight(0),
	bitmapStride(0),
	bitmapFormat(B_RGBA32),
	bitmapValid(false),
	bitmapDataOwned(false),

	frame(),
	mapped(false),
	window(NULL),
	serverWindow(NULL),
	opacity(1.0f),
	scale(1),
	transform(),
	dirty()
{
	wl_list_init(&attachedBufferDestroyListener.link);
	attachedBufferDestroyListener.notify = NULL;
}


WaylandSurface::~WaylandSurface()
{
	if (bitmapData != NULL && bitmapDataOwned) {
		free(bitmapData);
		bitmapData = NULL;
	}
	if (pool != NULL) {
		pool->buffers.RemoveItem(this);
		_PoolDataUnref(pool);
		pool = NULL;
	}
}


void
WaylandSurface::AcquireReference()
{
	referenceCount++;
}


void
WaylandSurface::ReleaseReference()
{
	if (--referenceCount == 0)
		delete this;
}


// ---------------------------------------------------------------------------
// Wayland global bind functions
// ---------------------------------------------------------------------------

static void
_compositor_bind(struct wl_client* client,
				 void* data,
				 uint32_t version,
				 uint32_t id)
{
	WaylandServer* server = static_cast<WaylandServer*>(data);
	if (server == NULL) {
		wl_client_post_no_memory(client);
		return;
	}

	struct wl_resource* compositor = wl_resource_create(client,
		&wl_compositor_interface, version, id);
	if (compositor == NULL) {
		wl_client_post_no_memory(client);
		return;
	}

	wl_resource_set_implementation(compositor, &sCompositorInterface,
		server, _compositor_destroy);
}


static void
_compositor_create_surface(struct wl_client* client,
						   struct wl_resource* resource,
						   uint32_t id)
{
	WLD_DEBUG("_compositor_create_surface id=%u", id);
	struct wl_resource* surface = wl_resource_create(client,
		&wl_surface_interface, wl_resource_get_version(resource), id);
	if (surface == NULL) {
		wl_client_post_no_memory(client);
		return;
	}

	WaylandServer* server = static_cast<WaylandServer*>(
		wl_resource_get_user_data(resource));

	WaylandSurface* ws = new (std::nothrow) WaylandSurface(surface);
	if (ws == NULL) {
		wl_resource_destroy(surface);
		wl_client_post_no_memory(client);
		return;
	}
	ws->server = server;

	wl_resource_set_implementation(surface, &sSurfaceInterface, ws,
		_SurfaceDestroy);

	if (server != NULL)
		server->AddSurface(ws);
}


static void
_compositor_create_region(struct wl_client* client,
						  struct wl_resource* resource,
						  uint32_t id)
{
	struct wl_resource* region = wl_resource_create(client,
		&wl_region_interface, wl_resource_get_version(resource), id);
	if (region == NULL) {
		wl_client_post_no_memory(client);
		return;
	}

	wl_resource_set_implementation(region, &sRegionInterface, NULL,
		_RegionDestroy);
}


static void
_shm_bind(struct wl_client* client,
		  void* data,
		  uint32_t version,
		  uint32_t id)
{
	struct wl_resource* shm = wl_resource_create(client,
		&wl_shm_interface, version, id);
	if (shm == NULL) {
		wl_client_post_no_memory(client);
		return;
	}
	wl_resource_set_implementation(shm, &sShmInterface, data,
		_ShmDestroy);

	// Advertise the formats our compositor understands.
	wl_shm_send_format(shm, WL_SHM_FORMAT_ARGB8888);
	wl_shm_send_format(shm, WL_SHM_FORMAT_XRGB8888);
}


static void
_seat_bind(struct wl_client* client,
		   void* data,
		   uint32_t version,
		   uint32_t id)
{
	struct wl_resource* seat = wl_resource_create(client,
		&wl_seat_interface, version, id);
	if (seat == NULL) {
		wl_client_post_no_memory(client);
		return;
	}

	WaylandServer* server = static_cast<WaylandServer*>(data);
	wl_resource_set_implementation(seat, &sSeatInterface, server,
		_SeatDestroy);

	wl_seat_send_capabilities(seat,
		WL_SEAT_CAPABILITY_POINTER | WL_SEAT_CAPABILITY_KEYBOARD);
}


static void
_xdg_wm_base_bind(struct wl_client* client,
				  void* data,
				  uint32_t version,
				  uint32_t id)
{
	WaylandServer* server = static_cast<WaylandServer*>(data);
	if (server == NULL) {
		wl_client_post_no_memory(client);
		return;
	}

	struct wl_resource* wm_base = wl_resource_create(client,
		&xdg_wm_base_interface, version, id);
	if (wm_base == NULL) {
		wl_client_post_no_memory(client);
		return;
	}

	wl_resource_set_implementation(wm_base, &sXdgWmBaseInterface, server,
		_XdgWmBaseDestroy);
}


// ---------------------------------------------------------------------------
// Surface lifecycle
// ---------------------------------------------------------------------------

static void
_SurfaceDestroyRequest(struct wl_client* client, struct wl_resource* resource)
{
	wl_resource_destroy(resource);
}


static void
_SurfaceDestroy(struct wl_resource* resource)
{
	WaylandSurface* surface = static_cast<WaylandSurface*>(
		wl_resource_get_user_data(resource));
	if (surface != NULL) {
		WaylandServer* server = surface->server;
		if (server != NULL)
			server->RemoveSurface(surface);

		_SurfaceReleaseAttachedBuffer(surface, true);

		surface->wlSurface = NULL;
		surface->ReleaseReference();
	}
}

static void
_SurfaceReleaseAttachedBuffer(WaylandSurface* surface, bool sendRelease)
{
	if (surface->attachedBuffer != NULL) {
		wl_list_remove(&surface->attachedBufferDestroyListener.link);
		wl_list_init(&surface->attachedBufferDestroyListener.link);
		if (sendRelease)
			wl_buffer_send_release(surface->attachedBuffer);
		surface->attachedBuffer = NULL;
	}
	if (surface->attachedBufferSurface != NULL) {
		surface->attachedBufferSurface->ReleaseReference();
		surface->attachedBufferSurface = NULL;
	}
}


static void
_AttachedBufferDestroyed(struct wl_listener* listener, void* data)
{
	WaylandSurface* surface = wl_container_of(listener, surface,
		attachedBufferDestroyListener);
	surface->attachedBuffer = NULL;
	wl_list_init(&surface->attachedBufferDestroyListener.link);
	(void)data;
}


static void
_SurfaceAttach(struct wl_client* client,
			   struct wl_resource* resource,
			   struct wl_resource* buffer,
			   int32_t x, int32_t y)
{
	WaylandSurface* surface = static_cast<WaylandSurface*>(
		wl_resource_get_user_data(resource));
	if (surface == NULL)
		return;

	if (surface->server != NULL)
		surface->server->LockSurfaces();

	WaylandSurface* bufferSurface = buffer != NULL
		? static_cast<WaylandSurface*>(wl_resource_get_user_data(buffer))
		: NULL;

	// Release any previously attached buffer that wasn't committed
	// (client replaced buffer without committing). We haven't copied
	// its data, so just tell the client they can reuse it.
	if (surface->attachedBufferSurface != bufferSurface) {
		_SurfaceReleaseAttachedBuffer(surface, true);
		surface->attachedBufferSurface = bufferSurface;
		if (bufferSurface != NULL) {
			bufferSurface->AcquireReference();
			surface->attachedBufferDestroyListener.notify
				= _AttachedBufferDestroyed;
			wl_resource_add_destroy_listener(buffer,
				&surface->attachedBufferDestroyListener);
		}
	}
	if (surface->attachedBuffer != buffer)
		surface->attachedBuffer = buffer;

	if (surface->attachedBufferSurface == NULL && buffer == NULL) {
		// A NULL attach releases the current pending buffer reference above.
		surface->bitmapWidth = 0;
		surface->bitmapHeight = 0;
		surface->bitmapStride = 0;
	}

	// Keep the cached allocation for reuse across commits. It remains invalid
	// until the newly attached buffer has been copied by _SurfaceCommit.
	surface->bitmapValid = false;

	if (surface->attachedBufferSurface != NULL) {
		WaylandSurface* bufSurface = surface->attachedBufferSurface;
		if (bufSurface != NULL) {
			surface->bitmapWidth = bufSurface->bitmapWidth;
			surface->bitmapHeight = bufSurface->bitmapHeight;
			surface->bitmapStride = bufSurface->bitmapStride;
			surface->bitmapFormat = bufSurface->bitmapFormat;
			// The cache is retained separately and refreshed in _SurfaceCommit.
			surface->bitmapSize = bufSurface->bitmapSize;
		}
	}

	// Do NOT mark dirty or repaint here: wl_surface.attach is a pending
	// operation; the buffer is not applied until wl_surface.commit.
	// Triggering a repaint now would paint stale / empty pixel data.

	WaylandServer* server = surface->server;
	if (server != NULL)
		server->UnlockSurfaces();
}


static void
_SurfaceDamage(struct wl_client* client,
			   struct wl_resource* resource,
			   int32_t x, int32_t y,
			   int32_t width, int32_t height)
{
	WaylandSurface* surface = static_cast<WaylandSurface*>(
		wl_resource_get_user_data(resource));
	if (surface == NULL)
		return;

	BRect damageRect(x, y, x + width - 1, y + height - 1);

	WaylandServer* server = surface->server;
	if (server != NULL)
		server->LockSurfaces();
	surface->dirty.Include(damageRect);
	if (server != NULL)
		server->UnlockSurfaces();

	if (server != NULL)
		server->MarkDirty(BRegion(damageRect));
}


static void
_SurfaceFrame(struct wl_client* client, struct wl_resource* resource,
			  uint32_t callback)
{
	// The compositor is synchronous: acknowledge the frame immediately.
	struct wl_resource* callbackResource = wl_resource_create(client,
		&wl_callback_interface, 1, callback);
	if (callbackResource == NULL) {
		wl_client_post_no_memory(client);
		return;
	}
	wl_callback_send_done(callbackResource, 0);
	wl_resource_destroy(callbackResource);
}


static void
_SurfaceSetOpaqueRegion(struct wl_client* client,
						struct wl_resource* resource,
						struct wl_resource* region)
{
	// Opaque regions are only a compositor optimization hint.
}


static void
_SurfaceSetInputRegion(struct wl_client* client,
					   struct wl_resource* resource,
					   struct wl_resource* region)
{
	// Input regions are not used yet; the whole surface accepts input.
}


static void
_SurfaceSetBufferTransform(struct wl_client* client,
						   struct wl_resource* resource,
						   int32_t transform)
{
	// Buffer transforms are not supported yet.
}


static void
_SurfaceSetBufferScale(struct wl_client* client,
					   struct wl_resource* resource,
					   int32_t scale)
{
	WaylandSurface* surface = static_cast<WaylandSurface*>(
		wl_resource_get_user_data(resource));
	if (surface != NULL && scale > 0)
		surface->scale = scale;
}


static void
_SurfaceDamageBuffer(struct wl_client* client,
					 struct wl_resource* resource,
					 int32_t x, int32_t y,
					 int32_t width, int32_t height)
{
	WaylandSurface* surface = static_cast<WaylandSurface*>(
		wl_resource_get_user_data(resource));
	if (surface == NULL)
		return;

	// Buffer coordinates scale to surface coordinates.
	WaylandServer* server = surface->server;
	if (server != NULL)
		server->LockSurfaces();
	int32 scale = surface->scale > 0 ? surface->scale : 1;
	BRect damageRect(x / scale, y / scale,
		(x + width) / scale - 1, (y + height) / scale - 1);
	surface->dirty.Include(damageRect);
	if (server != NULL)
		server->UnlockSurfaces();

	if (server != NULL)
		server->MarkDirty(BRegion(damageRect));
}


static void
_SurfaceOffset(struct wl_client* client, struct wl_resource* resource,
			   int32_t x, int32_t y)
{
	// Buffer offsets are accumulated by clients when attaching a sub-region
	// of a buffer; we always copy the full buffer, so there is nothing to do.
}


static void
_SurfaceCommit(struct wl_client* client,
			   struct wl_resource* resource)
{
	WLD_DEBUG("_SurfaceCommit");
	WaylandSurface* surface = static_cast<WaylandSurface*>(
		wl_resource_get_user_data(resource));
	if (surface == NULL)
		return;

	if (surface->server != NULL)
		surface->server->LockSurfaces();

	// Copy every committed frame into our own bitmap so the client can
	// immediately reuse its shm buffer.
	bool allocationFailed = false;
	if (surface->attachedBufferSurface != NULL
		&& surface->bitmapWidth > 0 && surface->bitmapHeight > 0
		&& surface->bitmapSize > 0) {
		WaylandSurface* bufSurface = surface->attachedBufferSurface;
		if (bufSurface->bitmapData != NULL
			&& (int64)bufSurface->bitmapStride
				>= (int64)surface->bitmapWidth * 4) {
			int64 rowBytes = (int64)surface->bitmapWidth * 4;
			int64 copySize = rowBytes * surface->bitmapHeight;
			int64 sourceSize = (int64)bufSurface->bitmapStride
				* surface->bitmapHeight;
			if (copySize > 0 && copySize <= INT32_MAX
				&& sourceSize <= bufSurface->bitmapSize) {
				bool reused = surface->bitmapDataOwned
					&& surface->bitmapCapacity >= copySize;
				void* copy = NULL;
				if (reused) {
					copy = surface->bitmapData;
				} else if (surface->bitmapDataOwned) {
					copy = realloc(surface->bitmapData, (size_t)copySize);
				} else
					copy = malloc((size_t)copySize);

				if (copy != NULL) {
					const uint8* source
						= static_cast<const uint8*>(bufSurface->bitmapData);
					uint8* destination = static_cast<uint8*>(copy);
					for (int32 row = 0; row < surface->bitmapHeight; row++) {
						memcpy(destination + row * rowBytes,
							source + row * bufSurface->bitmapStride,
							(size_t)rowBytes);
					}
					surface->bitmapData = copy;
					surface->bitmapSize = (int32)copySize;
					if (!reused)
						surface->bitmapCapacity = (int32)copySize;
					surface->bitmapStride = (int32)rowBytes;
					surface->bitmapValid = true;
					surface->bitmapDataOwned = true;
				} else
					allocationFailed = true;
			}
		}
	}

	if (allocationFailed) {
		if (surface->server != NULL)
			surface->server->UnlockSurfaces();
		wl_client_post_no_memory(client);
		return;
	}

	// Mark full surface dirty after new buffer data is committed
	surface->dirty = surface->frame;

	// Release the client's buffer - we've copied its data, so they can reuse it
	_SurfaceReleaseAttachedBuffer(surface, true);

	WaylandServer* server = surface->server;
	BRect frame = surface->frame;
	if (server != NULL)
		server->UnlockSurfaces();

	if (server != NULL)
		server->MarkDirty(BRegion(frame));
}


// ---------------------------------------------------------------------------
// Region
// ---------------------------------------------------------------------------

static void
_RegionDestroy(struct wl_resource* resource)
{
	(void)resource;
}

static void
_RegionDestroyRequest(struct wl_client* client,
					  struct wl_resource* resource)
{
	wl_resource_destroy(resource);
}


static void
_RegionAdd(struct wl_client* client,
		   struct wl_resource* resource,
		   int32_t x, int32_t y,
		   int32_t width, int32_t height)
{
	// Region operations not yet implemented
}


static void
_RegionSubtract(struct wl_client* client,
				struct wl_resource* resource,
				int32_t x, int32_t y,
				int32_t width, int32_t height)
{
	// Region operations not yet implemented
}





// ---------------------------------------------------------------------------
// shm
// ---------------------------------------------------------------------------

static void
_ShmDestroy(struct wl_resource* resource)
{
	(void)resource;
}


static void
_ShmRelease(struct wl_client* client, struct wl_resource* resource)
{
	wl_resource_destroy(resource);
}


static void
_ShmPoolDestroyRequest(struct wl_client* client,
					   struct wl_resource* resource)
{
	wl_resource_destroy(resource);
}


static void
_ShmPoolResize(struct wl_client* client, struct wl_resource* resource,
			   int32_t size)
{
	PoolData* pool = static_cast<PoolData*>(
		wl_resource_get_user_data(resource));
	if (pool == NULL || size <= pool->size) {
		wl_client_post_implementation_error(client,
			"wl_shm_pool.resize must increase the pool size");
		return;
	}

	struct stat poolStat;
	if (fstat(pool->fd, &poolStat) != 0 || poolStat.st_size < size) {
		wl_client_post_implementation_error(client,
			"shared memory file is smaller than the resized pool");
		return;
	}

	for (int32 i = 0; i < pool->buffers.CountItems(); i++) {
		WaylandSurface* buffer = pool->buffers.ItemAt(i);
		if (buffer != NULL
			&& (uint64_t)buffer->bitmapOffset
				+ (uint64_t)buffer->bitmapSize > (uint64_t)size) {
			wl_client_post_implementation_error(client,
				"resized pool would invalidate an existing buffer");
			return;
		}
	}

	void* data = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED,
		pool->fd, 0);
	if (data == MAP_FAILED) {
		wl_client_post_implementation_error(client,
			"failed to map resized shared memory pool");
		return;
	}

	void* oldData = pool->data;
	int32 oldSize = pool->size;
	pool->data = data;
	pool->size = size;
	for (int32 i = 0; i < pool->buffers.CountItems(); i++) {
		WaylandSurface* buffer = pool->buffers.ItemAt(i);
		if (buffer != NULL)
			buffer->bitmapData = static_cast<char*>(data)
				+ buffer->bitmapOffset;
	}
	munmap(oldData, oldSize);
}
static void
_PoolDataUnref(PoolData* pool)
{
	if (pool == NULL)
		return;

	if (--pool->refs > 0)
		return;

	if (pool->data != NULL)
		munmap(pool->data, pool->size);
	if (pool->fd >= 0)
		close(pool->fd);
	delete pool;
}


static void
_ShmCreatePool(struct wl_client* client,
			   struct wl_resource* resource,
			   uint32_t id, int32_t fd, int32_t size)
{
	struct stat poolStat;
	if (size <= 0 || fstat(fd, &poolStat) != 0
		|| poolStat.st_size < size) {
		close(fd);
		wl_resource_post_error(resource, WL_SHM_ERROR_INVALID_FD,
			"invalid shared memory pool");
		return;
	}

	struct wl_resource* pool = wl_resource_create(client,
		&wl_shm_pool_interface, wl_resource_get_version(resource), id);
	if (pool == NULL) {
		wl_client_post_no_memory(client);
		close(fd);
		return;
	}

	void* data = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	if (data == MAP_FAILED) {
		wl_resource_destroy(pool);
		wl_client_post_no_memory(client);
		close(fd);
		return;
	}

	PoolData* poolData = new (std::nothrow) PoolData;
	if (poolData == NULL) {
		munmap(data, size);
		wl_resource_destroy(pool);
		wl_client_post_no_memory(client);
		close(fd);
		return;
	}

	poolData->server = static_cast<WaylandServer*>(
		wl_resource_get_user_data(resource));
	poolData->data = data;
	poolData->fd = fd;
	poolData->size = size;
	poolData->refs = 1;  // the pool resource itself

	wl_resource_set_implementation(pool, &sShmPoolInterface, poolData,
		[](struct wl_resource* r) {
			_PoolDataUnref(static_cast<PoolData*>(
				wl_resource_get_user_data(r)));
		});
}


static void
_ShmPoolCreateBuffer(struct wl_client* client,
					 struct wl_resource* resource,
					 uint32_t id, int32_t offset,
					 int32_t width, int32_t height,
					 int32_t stride, uint32_t format)
{
	PoolData* pd = static_cast<PoolData*>(
		wl_resource_get_user_data(resource));
	if (pd == NULL || width <= 0 || height <= 0 || offset < 0
		|| stride <= 0 || (offset & 3) != 0 || (stride & 3) != 0) {
		wl_resource_post_error(resource, WL_SHM_ERROR_INVALID_STRIDE,
			"invalid shared memory buffer dimensions or stride");
		return;
	}

	if (format != WL_SHM_FORMAT_ARGB8888
		&& format != WL_SHM_FORMAT_XRGB8888) {
		wl_resource_post_error(resource, WL_SHM_ERROR_INVALID_FORMAT,
			"unsupported shared memory format");
		return;
	}

	uint64_t rowBytes = (uint64_t)width * 4;
	uint64_t bufferSize = (uint64_t)height * (uint64_t)stride;
	if (rowBytes > INT32_MAX || (uint64_t)stride < rowBytes
		|| bufferSize > INT32_MAX
		|| (uint64_t)offset + bufferSize > (uint64_t)pd->size) {
		wl_resource_post_error(resource, WL_SHM_ERROR_INVALID_STRIDE,
			"shared memory buffer exceeds pool bounds");
		return;
	}

	struct wl_resource* buffer = wl_resource_create(client,
		&wl_buffer_interface, wl_resource_get_version(resource), id);
	if (buffer == NULL) {
		wl_client_post_no_memory(client);
		return;
	}

	void* bufferData = NULL;
	int32 bufferSizeBytes = 0;
	if (pd->data != NULL) {
		bufferData = static_cast<char*>(pd->data) + offset;
		bufferSizeBytes = (int32)bufferSize;
	}

	WaylandSurface* bufSurface = new (std::nothrow) WaylandSurface(buffer);
	if (bufSurface == NULL) {
		wl_resource_destroy(buffer);
		wl_client_post_no_memory(client);
		return;
	}
	bufSurface->server = pd != NULL ? pd->server : NULL;
	bufSurface->pool = pd;
	if (pd != NULL)
		pd->refs++;
	bufSurface->bitmapData = bufferData;
	bufSurface->bitmapSize = bufferSizeBytes;
	bufSurface->bitmapOffset = offset;
	bufSurface->bitmapWidth = width;
	bufSurface->bitmapHeight = height;
	bufSurface->bitmapStride = stride;
	bufSurface->bitmapFormat = _wayland_format_to_colorspace(format);
	bufSurface->bitmapValid = (bufferData != NULL);
	bufSurface->bitmapDataOwned = false;
	if (!pd->buffers.AddItem(bufSurface)) {
		delete bufSurface;
		wl_resource_destroy(buffer);
		wl_client_post_no_memory(client);
		return;
	}

	wl_resource_set_implementation(buffer, &sBufferInterface, bufSurface,
		_BufferDestroy);
}


static void
_BufferDestroy(struct wl_resource* resource)
{
	WaylandSurface* surface = static_cast<WaylandSurface*>(
		wl_resource_get_user_data(resource));
	WaylandServer* server = surface != NULL ? surface->server : NULL;
	if (server != NULL)
		server->ForgetBuffer(resource);
	if (surface != NULL) {
		surface->wlSurface = NULL;
		surface->ReleaseReference();
	}
}


static void
_BufferRelease(struct wl_client* client, struct wl_resource* resource)
{
	wl_resource_destroy(resource);
}


static void
_SeatDestroy(struct wl_resource* resource)
{
	// Shared resource destructor for seat and touch.
	(void)resource;
}


static void
_SeatGetPointer(struct wl_client* client,
				struct wl_resource* resource,
				uint32_t id)
{
	struct wl_resource* pointer = wl_resource_create(client,
		&wl_pointer_interface, wl_resource_get_version(resource), id);
	if (pointer == NULL) {
		wl_client_post_no_memory(client);
		return;
	}

	WaylandServer* server = static_cast<WaylandServer*>(
		wl_resource_get_user_data(resource));
	if (server != NULL) {
		wl_resource_set_implementation(pointer, &sPointerInterface, server,
			_PointerDestroy);
		server->AddPointerResource(pointer);
	}
}


static void
_SeatGetKeyboard(struct wl_client* client,
				 struct wl_resource* resource,
				 uint32_t id)
{
	struct wl_resource* keyboard = wl_resource_create(client,
		&wl_keyboard_interface, wl_resource_get_version(resource), id);
	if (keyboard == NULL) {
		wl_client_post_no_memory(client);
		return;
	}

	WaylandServer* server = static_cast<WaylandServer*>(
		wl_resource_get_user_data(resource));
	if (server != NULL) {
		wl_resource_set_implementation(keyboard, &sKeyboardInterface, server,
			_KeyboardDestroy);
		server->AddKeyboardResource(keyboard);

		// Send an XKB keymap so clients can translate key codes to keysyms.
		// Most real Wayland clients (GTK, Qt, SDL2, Chromium) refuse to use
		// the keyboard at all with WL_KEYBOARD_KEYMAP_FORMAT_NO_KEYMAP.
		static const char kXkbKeymap[] =
			"xkb_keymap {\n"
			"  xkb_keycodes  { include \"evdev+aliases(qwerty)\" };\n"
			"  xkb_types     { include \"complete\" };\n"
			"  xkb_compat    { include \"complete\" };\n"
			"  xkb_symbols   { include \"pc+us+inet(evdev)\" };\n"
			"  xkb_geometry  { include \"pc(pc105)\" };\n"
			"};\n";

		// Write keymap to an anonymous temp file so we can hand an fd to
		// the client.  mkstemp + unlink gives us an anonymous file on any
		// POSIX system (no memfd_create required).
		char tmpPath[] = "/tmp/vos-xkb-XXXXXX";
		int kmFd = mkstemp(tmpPath);
		if (kmFd >= 0) {
			unlink(tmpPath);
			size_t keymapLen = strlen(kXkbKeymap) + 1; // include NUL
			ssize_t written = write(kmFd, kXkbKeymap, keymapLen);
			if (written == (ssize_t)keymapLen) {
				lseek(kmFd, 0, SEEK_SET);
				wl_keyboard_send_keymap(keyboard,
					WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1,
					kmFd, (uint32_t)keymapLen);
			}
			close(kmFd);
		}
	}
}


static void
_SeatGetTouch(struct wl_client* client,
			  struct wl_resource* resource,
			  uint32_t id)
{
	struct wl_resource* touch = wl_resource_create(client,
		&wl_touch_interface, wl_resource_get_version(resource), id);
	if (touch == NULL) {
		wl_client_post_no_memory(client);
		return;
	}
	wl_resource_set_implementation(touch, &sTouchInterface, NULL,
		_SeatDestroy);
}


static void
_SeatRelease(struct wl_client* client, struct wl_resource* resource)
{
	wl_resource_destroy(resource);
}





// ---------------------------------------------------------------------------
// pointer
// ---------------------------------------------------------------------------

static void
_PointerDestroy(struct wl_resource* resource)
{
	WaylandServer* server = static_cast<WaylandServer*>(
		wl_resource_get_user_data(resource));
	if (server != NULL)
		server->RemovePointerResource(resource);
}


static void
_PointerRelease(struct wl_client* client,
				struct wl_resource* resource)
{
	wl_resource_destroy(resource);
}


static void
_PointerSetCursor(struct wl_client* client,
				  struct wl_resource* resource,
				  uint32_t serial,
				  struct wl_resource* surface,
				  int32_t hotspot_x, int32_t hotspot_y)
{
	// Cursor setting not yet implemented
}





// ---------------------------------------------------------------------------
// keyboard
// ---------------------------------------------------------------------------

static void
_KeyboardDestroy(struct wl_resource* resource)
{
	WaylandServer* server = static_cast<WaylandServer*>(
		wl_resource_get_user_data(resource));
	if (server != NULL)
		server->RemoveKeyboardResource(resource);
}


static void
_KeyboardRelease(struct wl_client* client,
				 struct wl_resource* resource)
{
	wl_resource_destroy(resource);
}


static void
_TouchRelease(struct wl_client* client, struct wl_resource* resource)
{
	wl_resource_destroy(resource);
}





// ---------------------------------------------------------------------------
// xdg_wm_base
// ---------------------------------------------------------------------------

static void
_XdgWmBaseDestroy(struct wl_resource* resource)
{
	(void)resource;
}

static void
_XdgWmBaseDestroyRequest(struct wl_client* client,
						 struct wl_resource* resource)
{
	wl_resource_destroy(resource);
}


static void
_XdgWmBaseCreatePositioner(struct wl_client* client,
						   struct wl_resource* resource, uint32_t id)
{
	struct wl_resource* positioner = wl_resource_create(client,
		&xdg_positioner_interface, wl_resource_get_version(resource), id);
	if (positioner == NULL) {
		wl_client_post_no_memory(client);
		return;
	}
	wl_resource_set_implementation(positioner, &sXdgPositionerInterface,
		NULL, _XdgPositionerDestroy);
}


static void
_XdgWmBaseGetXdgSurface(struct wl_client* client,
						struct wl_resource* resource,
						uint32_t id,
						struct wl_resource* surface)
{
	WaylandSurface* ws = static_cast<WaylandSurface*>(
		wl_resource_get_user_data(surface));
	if (ws == NULL)
		return;

	struct wl_resource* xdg_surface = wl_resource_create(client,
		&xdg_surface_interface, wl_resource_get_version(resource), id);
	if (xdg_surface == NULL) {
		wl_client_post_no_memory(client);
		return;
	}

	ws->xdgSurface = xdg_surface;
	ws->AcquireReference();
	wl_resource_set_implementation(xdg_surface, &sXdgSurfaceInterface, ws,
		_XdgSurfaceDestroy);
}


static void
_XdgWmBasePong(struct wl_client* client,
			   struct wl_resource* resource,
			   uint32_t serial)
{
	// Ping/pong handled - client is alive
}





// ---------------------------------------------------------------------------
// xdg_surface
// ---------------------------------------------------------------------------

static void
_XdgSurfaceDestroy(struct wl_resource* resource)
{
	WaylandSurface* ws = static_cast<WaylandSurface*>(
		wl_resource_get_user_data(resource));
	if (ws != NULL && ws->xdgSurface == resource)
		ws->xdgSurface = NULL;
	if (ws != NULL)
		ws->ReleaseReference();
}

static void
_XdgSurfaceDestroyRequest(struct wl_client* client,
						  struct wl_resource* resource)
{
	wl_resource_destroy(resource);
}


static void
_XdgSurfaceGetToplevel(struct wl_client* client,
					   struct wl_resource* resource,
					   uint32_t id)
{
	WLD_DEBUG("_XdgSurfaceGetToplevel");
	WaylandSurface* ws = static_cast<WaylandSurface*>(
		wl_resource_get_user_data(resource));
	if (ws == NULL)
		return;

	struct wl_resource* toplevel = wl_resource_create(client,
		&xdg_toplevel_interface, wl_resource_get_version(resource), id);
	if (toplevel == NULL) {
		wl_client_post_no_memory(client);
		return;
	}

	wl_resource_set_implementation(toplevel, &sXdgToplevelInterface, ws,
		_XdgToplevelDestroy);
	ws->AcquireReference();

	ws->mapped = true;

	// xdg-shell requires an initial configure before the client draws.
	struct wl_display* display = wl_client_get_display(client);
	struct wl_array states;
	wl_array_init(&states);
	xdg_toplevel_send_configure(toplevel, 0, 0, &states);
	wl_array_release(&states);
	xdg_surface_send_configure(resource, wl_display_next_serial(display));

	// Create a native Window for this Wayland surface
	if (ws->server != NULL) {
		ServerWindow* sw = NULL;
		BRect frame = ws->frame.IsValid() ? ws->frame
			: BRect(0, 0, ws->bitmapWidth - 1, ws->bitmapHeight - 1);
		if (!frame.IsValid())
			frame = BRect(0, 0, 100, 100);
		Window* window = ws->server->CreateNativeWindow(frame, &sw);
		if (window != NULL) {
			ws->window = window;
			ws->serverWindow = sw;
		}
	}
}


static void
_XdgSurfaceGetPopup(struct wl_client* client, struct wl_resource* resource,
					uint32_t id, struct wl_resource* parent,
					struct wl_resource* positioner)
{
	WaylandSurface* ws = static_cast<WaylandSurface*>(
		wl_resource_get_user_data(resource));
	if (ws == NULL)
		return;

	struct wl_resource* popup = wl_resource_create(client,
		&xdg_popup_interface, wl_resource_get_version(resource), id);
	if (popup == NULL) {
		wl_client_post_no_memory(client);
		return;
	}
	wl_resource_set_implementation(popup, &sXdgPopupInterface, ws,
		_XdgPopupDestroy);
	ws->AcquireReference();

	xdg_popup_send_configure(popup, 0, 0, 0, 0);
	xdg_surface_send_configure(resource,
		wl_display_next_serial(wl_client_get_display(client)));

	// A popup is a transient role; the surface is already mapped as a
	// toplevel in this minimal compositor, so keep the native window.
}


static void
_XdgSurfaceAckConfigure(struct wl_client* client,
						struct wl_resource* resource, uint32_t serial)
{
	// We never send configure events that require an acknowledgement,
	// but accepting the ack keeps clients happy.
}


static void
_XdgSurfaceSetWindowGeometry(struct wl_client* client,
							 struct wl_resource* resource,
							 int32_t x, int32_t y,
							 int32_t width, int32_t height)
{
	WaylandSurface* ws = static_cast<WaylandSurface*>(
		wl_resource_get_user_data(resource));
	if (ws == NULL)
		return;

	ws->frame = BRect(x, y, x + width - 1, y + height - 1);
	ws->dirty = ws->frame;

	WaylandServer* server = ws->server;
	if (server != NULL)
		server->MarkDirty(BRegion(ws->frame));
}


// ---------------------------------------------------------------------------
// xdg_toplevel
// ---------------------------------------------------------------------------

static void
_XdgToplevelDestroy(struct wl_resource* resource)
{
	WaylandSurface* ws = static_cast<WaylandSurface*>(
		wl_resource_get_user_data(resource));
	if (ws != NULL) {
		// Destroy the native Window via Desktop
		if (ws->window != NULL) {
			WaylandServer* server = ws->server;
			if (server != NULL)
				server->DestroyNativeWindow(ws->window);
			ws->window = NULL;
		}
		// Delete the ServerWindow (it removes itself from Desktop on destruction)
		if (ws->serverWindow != NULL) {
			delete ws->serverWindow;
			ws->serverWindow = NULL;
		}
		ws->window = NULL;
		ws->mapped = false;
		ws->ReleaseReference();
	}
}

static void
_XdgToplevelDestroyRequest(struct wl_client* client,
						   struct wl_resource* resource)
{
	wl_resource_destroy(resource);
}


static void
_XdgToplevelSetParent(struct wl_client* client,
					  struct wl_resource* resource,
					  struct wl_resource* parent)
{
	// Parent tracking not yet implemented
}


static void
_XdgToplevelSetTitle(struct wl_client* client,
					 struct wl_resource* resource,
					 const char* title)
{
	WaylandSurface* ws = static_cast<WaylandSurface*>(
		wl_resource_get_user_data(resource));
	if (ws == NULL || ws->window == NULL || ws->server == NULL)
		return;

	ws->server->SetNativeWindowTitle(ws->window, title);
}


static void
_XdgToplevelSetAppId(struct wl_client* client,
					 struct wl_resource* resource,
					 const char* app_id)
{
	// App ID tracking not yet implemented
}


static void
_XdgToplevelShowWindowMenu(struct wl_client* client,
						   struct wl_resource* resource,
						   struct wl_resource* seat,
						   uint32_t serial,
						   int32_t x, int32_t y)
{
	// Window menu not yet implemented
}


static void
_XdgToplevelMove(struct wl_client* client,
				 struct wl_resource* resource,
				 struct wl_resource* seat,
				 uint32_t serial)
{
	// Interactive move not yet implemented
}


static void
_XdgToplevelResize(struct wl_client* client,
				   struct wl_resource* resource,
				   struct wl_resource* seat,
				   uint32_t serial, uint32_t edges)
{
	// Interactive resize not yet implemented
}


static void
_XdgToplevelSetMaxSize(struct wl_client* client,
					   struct wl_resource* resource,
					   int32_t width, int32_t height)
{
	// Max size not yet implemented
}


static void
_XdgToplevelSetMinSize(struct wl_client* client,
					   struct wl_resource* resource,
					   int32_t width, int32_t height)
{
	// Min size not yet implemented
}


static void
_XdgToplevelSetMaximized(struct wl_client* client,
						 struct wl_resource* resource)
{
	// Maximize not yet implemented
}


static void
_XdgToplevelUnsetMaximized(struct wl_client* client,
						   struct wl_resource* resource)
{
	// Unmaximize not yet implemented
}


static void
_XdgToplevelSetMinimized(struct wl_client* client,
						 struct wl_resource* resource)
{
	WaylandSurface* ws = static_cast<WaylandSurface*>(
		wl_resource_get_user_data(resource));
	if (ws == NULL || ws->window == NULL || ws->server == NULL)
		return;

	ws->server->MinimizeNativeWindow(ws->window);
}


static void
_XdgToplevelSetFullscreen(struct wl_client* client,
						  struct wl_resource* resource,
						  struct wl_resource* output)
{
	// Fullscreen not yet implemented
}


static void
_XdgToplevelUnsetFullscreen(struct wl_client* client,
							struct wl_resource* resource)
{
	// Unfullscreen not yet implemented
}


// ---------------------------------------------------------------------------
// xdg_positioner
// ---------------------------------------------------------------------------

static void
_XdgPositionerDestroy(struct wl_resource* resource)
{
	(void)resource;
}

static void
_XdgPositionerDestroyRequest(struct wl_client* client,
							 struct wl_resource* resource)
{
	wl_resource_destroy(resource);
}


static void
_XdgPositionerSetSize(struct wl_client* client,
					  struct wl_resource* resource,
					  int32_t width, int32_t height)
{
}


static void
_XdgPositionerSetAnchorRect(struct wl_client* client,
							struct wl_resource* resource,
							int32_t x, int32_t y,
							int32_t width, int32_t height)
{
}


static void
_XdgPositionerSetAnchor(struct wl_client* client,
						struct wl_resource* resource, uint32_t anchor)
{
}


static void
_XdgPositionerSetGravity(struct wl_client* client,
						 struct wl_resource* resource, uint32_t gravity)
{
}


static void
_XdgPositionerSetConstraintAdjustment(struct wl_client* client,
									  struct wl_resource* resource,
									  uint32_t constraint_adjustment)
{
}


static void
_XdgPositionerSetOffset(struct wl_client* client,
						struct wl_resource* resource,
						int32_t x, int32_t y)
{
}


static void
_XdgPositionerSetReactive(struct wl_client* client,
						  struct wl_resource* resource)
{
}


static void
_XdgPositionerSetParentSize(struct wl_client* client,
							struct wl_resource* resource,
							int32_t parent_width,
							int32_t parent_height)
{
}


static void
_XdgPositionerSetParentConfigure(struct wl_client* client,
								 struct wl_resource* resource,
								 uint32_t serial)
{
}


// ---------------------------------------------------------------------------
// xdg_popup
// ---------------------------------------------------------------------------

static void
_XdgPopupDestroy(struct wl_resource* resource)
{
	WaylandSurface* ws = static_cast<WaylandSurface*>(
		wl_resource_get_user_data(resource));
	if (ws != NULL)
		ws->ReleaseReference();
}

static void
_XdgPopupDestroyRequest(struct wl_client* client,
						struct wl_resource* resource)
{
	wl_resource_destroy(resource);
}


static void
_XdgPopupGrab(struct wl_client* client, struct wl_resource* resource,
			  struct wl_resource* seat, uint32_t serial)
{
}


static void
_XdgPopupReposition(struct wl_client* client, struct wl_resource* resource,
					struct wl_resource* positioner, uint32_t token)
{
}


// ---------------------------------------------------------------------------
// WaylandServer
// ---------------------------------------------------------------------------

static void
_LogProtocolArgument(FILE* log, char type, const union wl_argument& argument)
{
	switch (type) {
		case 'i':
			fprintf(log, "%d", argument.i);
			break;
		case 'u':
			fprintf(log, "%u", argument.u);
			break;
		case 'f':
			fprintf(log, "%g", wl_fixed_to_double(argument.f));
			break;
		case 's': {
			if (argument.s == NULL) {
				fprintf(log, "null");
				break;
			}

			fputc('"', log);
			const unsigned char* character
				= reinterpret_cast<const unsigned char*>(argument.s);
			for (; *character != '\0'; character++) {
				if (*character == '"' || *character == '\\') {
					fputc('\\', log);
					fputc(*character, log);
				} else if (*character == '\n') {
					fprintf(log, "\\n");
				} else if (*character == '\r') {
					fprintf(log, "\\r");
				} else if (*character == '\t') {
					fprintf(log, "\\t");
				} else if (*character < 0x20 || *character == 0x7f) {
					fprintf(log, "\\x%02x", *character);
				} else {
					fputc(*character, log);
				}
			}
			fputc('"', log);
			break;
		}
		case 'o':
			if (argument.o == NULL) {
				fprintf(log, "null");
			} else {
				fprintf(log, "%s@%u", argument.o->interface->name,
					argument.o->id);
			}
			break;
		case 'n':
			fprintf(log, "%u", argument.n);
			break;
		case 'a':
			fprintf(log, "array[%zu]",
				argument.a != NULL ? argument.a->size : 0);
			break;
		case 'h':
			fprintf(log, "fd(%d)", argument.h);
			break;
		default:
			fprintf(log, "<?>");
			break;
	}
}


static void
_ProtocolLogger(void* userData, enum wl_protocol_logger_type direction,
	const struct wl_protocol_logger_message* message)
{
	FILE* log = static_cast<FILE*>(userData);
	struct wl_resource* resource = message->resource;
	struct wl_client* client = wl_resource_get_client(resource);
	pid_t pid = 0;
	uid_t uid = 0;
	gid_t gid = 0;
	wl_client_get_credentials(client, &pid, &uid, &gid);

	struct timeval now;
	gettimeofday(&now, NULL);
	struct tm localTime;
	char timestamp[32] = "";
	if (localtime_r(&now.tv_sec, &localTime) != NULL) {
		strftime(timestamp, sizeof(timestamp), "%Y-%m-%dT%H:%M:%S",
			&localTime);
	}

	fprintf(log, "%s.%03ld %s pid=%ld uid=%ld gid=%ld %s@%u(v%u).%s(",
		timestamp, now.tv_usec / 1000,
		direction == WL_PROTOCOL_LOGGER_REQUEST ? "C->S" : "S->C",
		(long)pid, (long)uid, (long)gid,
		wl_resource_get_class(resource), wl_resource_get_id(resource),
		wl_resource_get_version(resource), message->message->name);

	const char* signature = message->message->signature;
	while (*signature >= '0' && *signature <= '9')
		signature++;

	for (int i = 0; i < message->arguments_count; i++) {
		while (*signature == '?')
			signature++;
		if (i > 0)
			fprintf(log, ", ");

		fprintf(log, "arg%d=", i);
		if (*signature != '\0') {
			_LogProtocolArgument(log, *signature,
				message->arguments[i]);
			signature++;
		} else {
			fprintf(log, "<?>");
		}
	}

	fprintf(log, ")\n");
	fflush(log);
}


WaylandServer::WaylandServer(Desktop* desktop)
	:
	fDesktop(desktop),
	fInitStatus(B_ERROR),
	fDisplay(NULL),
	fProtocolLogger(NULL),
	fProtocolLog(NULL),
	fSocketFd(-1),
	fDispatchThread(-1),
	fRunning(false),
	fPointerX(0),
	fPointerY(0),
	fLastSerial(0),
	fFocusSurface(NULL),
	fSurfaces(10),
	fPointerResources(4),
	fKeyboardResources(4)
{
	fDisplay = wl_display_create();
	if (fDisplay == NULL) {
		fprintf(stderr, "WaylandServer: wl_display_create() failed\n");
		return;
	}

	// Register globals
	wl_global_create(fDisplay, &wl_compositor_interface, 4, this,
		_compositor_bind);

	wl_global_create(fDisplay, &wl_shm_interface, 1, this,
		_shm_bind);

	// GTK waits for this global before binding wl_seat.
	if (CreateWaylandDataDeviceManagerGlobal(fDisplay) == NULL) {
		fprintf(stderr, "WaylandServer: failed to create data device manager global\n");
		wl_display_destroy(fDisplay);
		fDisplay = NULL;
		return;
	}

	wl_global_create(fDisplay, &wl_seat_interface, 1, this,
		_seat_bind);

	wl_global_create(fDisplay, &wl_output_interface, 3, this,
		_output_bind);

	// Do not advertise wl_subcompositor until subsurface roles are handled.
	// A resource with no request implementation is not a usable global.
	// Viewporter and fractional-scale remain disabled until their state is
	// applied to buffer rendering and output scale.

	wl_global_create(fDisplay, &xdg_wm_base_interface, 1, this,
		_xdg_wm_base_bind);

	// Create the Wayland socket. libwayland picks the first free name
	// (normally "wayland-0") and returns it; clients use that name in
	// WAYLAND_DISPLAY, relative to XDG_RUNTIME_DIR.
	const char* socketName = wl_display_add_socket_auto(fDisplay);
	if (socketName == NULL) {
		fprintf(stderr, "WaylandServer: wl_display_add_socket_auto() failed\n");
		wl_display_destroy(fDisplay);
		fDisplay = NULL;
		return;
	}
	fSocketName = socketName;
	fSocketFd = 0;

	const char* protocolLogPath = getenv("VOS_WAYLAND_PROTOCOL_LOG");
	if (protocolLogPath != NULL && protocolLogPath[0] != '\0') {
		fProtocolLog = fopen(protocolLogPath, "a");
		if (fProtocolLog == NULL) {
			int error = errno;
			fprintf(stderr, "WaylandServer: cannot open protocol log %s: %s\n",
				protocolLogPath, strerror(error));
		} else {
			fProtocolLogger = wl_display_add_protocol_logger(fDisplay,
				_ProtocolLogger, fProtocolLog);
			if (fProtocolLogger == NULL) {
				fprintf(stderr,
					"WaylandServer: cannot enable protocol logging\n");
				fclose(fProtocolLog);
				fProtocolLog = NULL;
			} else {
				fprintf(fProtocolLog,
					"--- Wayland protocol log started: socket=%s pid=%ld ---\n",
					socketName, (long)getpid());
				fflush(fProtocolLog);
			}
		}
	}

	fInitStatus = B_OK;
}


WaylandServer::~WaylandServer()
{
	Stop();

	if (fDisplay != NULL) {
		if (fProtocolLogger != NULL) {
			wl_protocol_logger_destroy(fProtocolLogger);
			fProtocolLogger = NULL;
		}
		if (fProtocolLog != NULL) {
			fprintf(fProtocolLog, "--- Wayland protocol log stopped ---\n");
			fclose(fProtocolLog);
			fProtocolLog = NULL;
		}

		// Resource destroy callbacks can refer to their owning surfaces.
		// Tear clients down while those resources still hold references.
		wl_display_destroy_clients(fDisplay);
		wl_display_destroy(fDisplay);
		fDisplay = NULL;
	}
}


int32
WaylandServer::_DispatchThreadEntry(void* arg)
{
	WaylandServer* self = static_cast<WaylandServer*>(arg);
	self->_DispatchThread();
	return 0;
}


void
WaylandServer::_DispatchThread()
{
	WLD_DEBUG("Wayland display thread starting");
	// wl_display_run() blocks until wl_display_terminate() is called,
	// dispatching Wayland events and flushing clients internally.
	wl_display_run(fDisplay);
	WLD_DEBUG("Wayland display thread exiting");
}


bool
WaylandServer::Run()
{
	if (fDisplay == NULL || fRunning)
		return false;

	fRunning = true;

	fDispatchThread = spawn_thread(_DispatchThreadEntry,
		"wayland dispatch", B_NORMAL_PRIORITY, this);
	if (fDispatchThread < B_OK) {
		fRunning = false;
		return false;
	}

	resume_thread(fDispatchThread);
	return true;
}


void
WaylandServer::Stop()
{
	if (!fRunning)
		return;

	fRunning = false;

	if (fDispatchThread >= B_OK) {
		wl_display_terminate(fDisplay);

		status_t result;
		wait_for_thread(fDispatchThread, &result);
		fDispatchThread = -1;
	}
}


int32
WaylandServer::CountSurfaces() const
{
	return fSurfaces.CountItems();
}


WaylandSurface*
WaylandServer::SurfaceAt(int32 index) const
{
	return fSurfaces.ItemAt(index);
}


WaylandSurface*
WaylandServer::FindSurface(struct wl_resource* resource) const
{
	for (int32 i = 0; i < fSurfaces.CountItems(); i++) {
		WaylandSurface* ws = fSurfaces.ItemAt(i);
		if (ws->wlSurface == resource || ws->xdgSurface == resource)
			return ws;
	}
	return NULL;
}


void
WaylandServer::AddSurface(WaylandSurface* surface)
{
	fLock.Lock();
	fSurfaces.AddItem(surface);
	fLock.Unlock();
}


void
WaylandServer::RemoveSurface(WaylandSurface* surface)
{
	fLock.Lock();
	if (surface != NULL && fFocusSurface == surface->wlSurface)
		fFocusSurface = NULL;
	fSurfaces.RemoveItem(surface);
	fLock.Unlock();
}


void
WaylandServer::MarkDirty(const BRegion& region)
{
	if (fDesktop == NULL)
		return;

	BRegion copy(region);
	fDesktop->MarkDirty(copy);
}


void
WaylandServer::DestroyNativeWindow(Window* window)
{
	if (fDesktop != NULL && window != NULL)
		fDesktop->DestroyWaylandWindow(window);
}


Window*
WaylandServer::CreateNativeWindow(const BRect& frame, ServerWindow** serverWindow)
{
	if (fDesktop == NULL)
		return NULL;
	return fDesktop->CreateWaylandWindow(frame, "Wayland Client",
		serverWindow);
}


void
WaylandServer::SetNativeWindowTitle(Window* window, const char* title)
{
	if (fDesktop != NULL && window != NULL)
		fDesktop->SetWaylandWindowTitle(window, title);
}


void
WaylandServer::MinimizeNativeWindow(Window* window)
{
	if (fDesktop != NULL && window != NULL)
		fDesktop->SetWaylandWindowMinimized(window, true);
}


void
WaylandServer::ForgetBuffer(struct wl_resource* buffer)
{
	fLock.Lock();
	for (int32 i = 0; i < fSurfaces.CountItems(); i++) {
		WaylandSurface* surface = fSurfaces.ItemAt(i);
		if (surface != NULL && surface->attachedBuffer == buffer)
			surface->attachedBuffer = NULL;
	}
	fLock.Unlock();
}


void
WaylandServer::SetOpacity(WaylandSurface* surface, float opacity)
{
	if (surface == NULL)
		return;
	surface->opacity = opacity;
}


void
WaylandServer::SetTransform(WaylandSurface* surface,
	const BAffineTransform& transform)
{
	if (surface == NULL)
		return;
	surface->transform = transform;
}


status_t
WaylandServer::PaintSurfaces(DrawingEngine* engine, const BRegion& dirtyRegion)
{
#ifdef VOS_WAYLAND
	if (engine == NULL || !fRunning)
		return B_BAD_VALUE;

	if (!engine->LockParallelAccess())
		return B_ERROR;

	fLock.Lock();

	status_t result = B_OK;

	// Save current drawing mode
	drawing_mode oldMode;
	engine->SetDrawingMode(B_OP_COPY, oldMode);

	for (int32 i = 0; i < fSurfaces.CountItems(); i++) {
		WaylandSurface* ws = fSurfaces.ItemAt(i);
		if (ws == NULL || !ws->bitmapValid || ws->bitmapData == NULL)
			continue;

		// Check if surface intersects the requested dirty region
		if (!dirtyRegion.Intersects(ws->frame))
			continue;

		// Compute per-surface damage: intersect global dirty region with
		// surface's accumulated damage region. If surface has no damage,
		// skip it.
		BRegion surfaceDirty(ws->dirty);
		surfaceDirty.IntersectWith(&dirtyRegion);
		if (surfaceDirty.CountRects() == 0)
			continue;

		// Create a temporary UtilityBitmap wrapping our cached data
		// UtilityBitmap wraps external data without copying
		UtilityBitmap bitmap((const uint8*)ws->bitmapData, ws->bitmapWidth,
			ws->bitmapHeight, ws->bitmapFormat);

		if (!bitmap.IsValid())
			continue;

		// Apply surface transform
		BAffineTransform identity;
		engine->SetTransform(ws->transform, 0, 0);

		// Clip drawing to only the damaged area
		engine->ConstrainClippingRegion(&surfaceDirty);

		// Apply opacity if needed
		if (ws->opacity < 1.0f) {
			// Draw a semi-transparent white overlay first, then the bitmap
			// This gives: result = opacity * bitmap + (1-opacity) * white
			rgb_color overlayColor = (rgb_color){
				(uint8)((1.0f - ws->opacity) * 255),
				(uint8)((1.0f - ws->opacity) * 255),
				(uint8)((1.0f - ws->opacity) * 255),
				255 };
			engine->SetHighColor(overlayColor);
			engine->SetDrawingMode(B_OP_OVER);
			engine->FillRect(ws->frame);
			engine->SetDrawingMode(oldMode);
		}

		// Draw the bitmap at the surface's position
		BRect srcRect(0, 0, ws->bitmapWidth - 1, ws->bitmapHeight - 1);
		BRect dstRect = ws->frame;

		engine->DrawBitmap(&bitmap, srcRect, dstRect);

		// Restore drawing mode
		engine->SetDrawingMode(oldMode);

		// Clear surface damage region after successful paint
		ws->dirty.MakeEmpty();

		// Clear clipping so it doesn't affect subsequent surfaces
		engine->ConstrainClippingRegion(NULL);

		// Reset transform to identity for next surface
		engine->SetTransform(identity, 0, 0);
	}

	fLock.Unlock();
	engine->UnlockParallelAccess();

	return result;
#else
	return B_ERROR;
#endif
}


void
WaylandServer::AddPointerResource(struct wl_resource* resource)
{
	fLock.Lock();
	fPointerResources.AddItem(resource);
	fLock.Unlock();
}


void
WaylandServer::RemovePointerResource(struct wl_resource* resource)
{
	fLock.Lock();
	fPointerResources.RemoveItem(resource);
	fLock.Unlock();
}


void
WaylandServer::AddKeyboardResource(struct wl_resource* resource)
{
	fLock.Lock();
	fKeyboardResources.AddItem(resource);
	fLock.Unlock();
}


void
WaylandServer::RemoveKeyboardResource(struct wl_resource* resource)
{
	fLock.Lock();
	fKeyboardResources.RemoveItem(resource);
	fLock.Unlock();
}


void
WaylandServer::SendPointerMotion(int32 x, int32 y, uint32 time)
{
#ifdef VOS_WAYLAND
	fLock.Lock();

	// Find the topmost mapped surface under the pointer. The list is in
	// stacking order, so search from the end.
	// Also require bitmapValid: a surface that has a toplevel role assigned
	// (mapped = true) but has not yet committed its first buffer should not
	// receive pointer events - its frame rect may be the 100x100 fallback.
	WaylandSurface* target = NULL;
	for (int32 i = fSurfaces.CountItems() - 1; i >= 0; i--) {
		WaylandSurface* ws = fSurfaces.ItemAt(i);
		if (ws != NULL && ws->mapped && ws->bitmapValid
			&& ws->frame.Contains(BPoint(x, y))) {
			target = ws;
			break;
		}
	}

	struct wl_resource* targetResource
		= target != NULL ? target->wlSurface : NULL;
	struct wl_client* oldClient = fFocusSurface != NULL
		? wl_resource_get_client(fFocusSurface) : NULL;
	struct wl_client* targetClient = targetResource != NULL
		? wl_resource_get_client(targetResource) : NULL;

	if (targetResource != fFocusSurface) {
		uint32 serial = ++fLastSerial;

		if (fFocusSurface != NULL) {
			for (int32 i = 0; i < fPointerResources.CountItems(); i++) {
				struct wl_resource* pointer = fPointerResources.ItemAt(i);
				if (WaylandResourceBelongsToClient(pointer, oldClient)) {
					wl_pointer_send_leave(pointer, serial, fFocusSurface);
				}
			}
			for (int32 i = 0; i < fKeyboardResources.CountItems(); i++) {
				struct wl_resource* keyboard = fKeyboardResources.ItemAt(i);
				if (WaylandResourceBelongsToClient(keyboard, oldClient)) {
					wl_keyboard_send_leave(keyboard, serial, fFocusSurface);
				}
			}
		}

		if (target != NULL) {
			int32 localX = x - target->frame.left;
			int32 localY = y - target->frame.top;

			for (int32 i = 0; i < fPointerResources.CountItems(); i++) {
				struct wl_resource* pointer = fPointerResources.ItemAt(i);
				if (WaylandResourceBelongsToClient(pointer, targetClient)) {
					wl_pointer_send_enter(pointer, serial, targetResource,
						wl_fixed_from_double(localX),
						wl_fixed_from_double(localY));
				}
			}

			struct wl_array keys;
			wl_array_init(&keys);
			for (int32 i = 0; i < fKeyboardResources.CountItems(); i++) {
				struct wl_resource* keyboard = fKeyboardResources.ItemAt(i);
				if (WaylandResourceBelongsToClient(keyboard, targetClient)) {
					wl_keyboard_send_enter(keyboard, serial, targetResource,
						&keys);
				}
			}
			wl_array_release(&keys);
		}

		fFocusSurface = targetResource;
	}

	if (target != NULL) {
		int32 localX = x - target->frame.left;
		int32 localY = y - target->frame.top;
		for (int32 i = 0; i < fPointerResources.CountItems(); i++) {
			struct wl_resource* pointer = fPointerResources.ItemAt(i);
			if (WaylandResourceBelongsToClient(pointer, targetClient)) {
				wl_pointer_send_motion(pointer, time,
					wl_fixed_from_double(localX),
					wl_fixed_from_double(localY));
			}
		}
	}

	fPointerX = x;
	fPointerY = y;

	fLock.Unlock();
#endif
}


void
WaylandServer::SendPointerButton(uint32 button, uint32 state, uint32 time)
{
#ifdef VOS_WAYLAND
	fLock.Lock();

	if (fFocusSurface == NULL) {
		fLock.Unlock();
		return;
	}
	struct wl_client* focusClient = wl_resource_get_client(fFocusSurface);

	// Desktop passes the BeOS button mask (1 = primary, 2 = secondary,
	// 4 = tertiary); Wayland expects Linux evdev button codes.
	uint32_t wlButton = button;
	switch (button) {
		case 1: wlButton = 0x110; break;  // left
		case 2: wlButton = 0x111; break;  // right
		case 4: wlButton = 0x112; break;  // middle
		default: wlButton = button; break;
	}

	uint32_t wlState = state == 1 ? WL_POINTER_BUTTON_STATE_PRESSED
								  : WL_POINTER_BUTTON_STATE_RELEASED;

	fLastSerial++;

	for (int32 i = 0; i < fPointerResources.CountItems(); i++) {
		struct wl_resource* ptr = fPointerResources.ItemAt(i);
		if (WaylandResourceBelongsToClient(ptr, focusClient)) {
			wl_pointer_send_button(ptr, fLastSerial, time, wlButton, wlState);
		}
	}

	fLock.Unlock();
#endif
}


void
WaylandServer::SendKeyboardKey(uint32 key, uint32 state, uint32 time,
							   uint32 modifiers)
{
#ifdef VOS_WAYLAND
	fLock.Lock();

	// Keyboard events must only be sent to resources owned by the focused
	// surface's client. Sending a surface to another client's keyboard
	// resource violates the Wayland protocol and disconnects that client.
	if (fFocusSurface == NULL) {
		fLock.Unlock();
		return;
	}
	struct wl_client* focusClient = wl_resource_get_client(fFocusSurface);

	uint32_t wlState = state == 1 ? WL_KEYBOARD_KEY_STATE_PRESSED
								  : WL_KEYBOARD_KEY_STATE_RELEASED;

	fLastSerial++;

	for (int32 i = 0; i < fKeyboardResources.CountItems(); i++) {
		struct wl_resource* kbd = fKeyboardResources.ItemAt(i);
		if (WaylandResourceBelongsToClient(kbd, focusClient)) {
			wl_keyboard_send_modifiers(kbd, fLastSerial, modifiers,
				0, 0, 0);
			wl_keyboard_send_key(kbd, fLastSerial, time, key, wlState);
		}
	}

	fLock.Unlock();
#endif
}


// ---------------------------------------------------------------------------
// Interface definitions
// ---------------------------------------------------------------------------



















static const struct wl_output_interface sOutputInterface = {
	_output_release
};


static void
_output_release(struct wl_client* client, struct wl_resource* resource)
{
	(void)client;
	wl_resource_destroy(resource);
}


static void
_output_bind(struct wl_client* client,
			 void* data,
			 uint32_t version,
			 uint32_t id)
{
	struct wl_resource* output = wl_resource_create(client,
		&wl_output_interface, version, id);
	if (output == NULL) {
		wl_client_post_no_memory(client);
		return;
	}

	wl_resource_set_implementation(output, &sOutputInterface, data, NULL);

	WaylandServer* server = static_cast<WaylandServer*>(data);
	Desktop* desktop = server ? server->GetDesktop() : NULL;
	if (desktop == NULL || desktop->VirtualScreen().CountScreens() <= 0) {
		wl_resource_destroy(output);
		wl_client_post_implementation_error(client,
			"cannot bind wl_output without an active screen");
		return;
	}

	Screen* screen = desktop->VirtualScreen().ScreenAt(0);
	if (screen == NULL) {
		wl_resource_destroy(output);
		wl_client_post_implementation_error(client,
			"primary screen is unavailable");
		return;
	}

	BRect frame = screen->Frame();
	if (!frame.IsValid()) {
		wl_resource_destroy(output);
		wl_client_post_implementation_error(client,
			"primary screen has invalid dimensions");
		return;
	}

	float frameWidth = frame.Width() + 1;
	float frameHeight = frame.Height() + 1;
	if (frameWidth <= 0 || frameHeight <= 0
		|| frameWidth >= (float)INT32_MAX
		|| frameHeight >= (float)INT32_MAX) {
		wl_resource_destroy(output);
		wl_client_post_implementation_error(client,
			"primary screen has invalid dimensions");
		return;
	}
	int32 width = (int32)frameWidth;
	int32 height = (int32)frameHeight;

	display_mode mode;
	screen->GetMode(mode);
	int32 refresh = 60000;
	if (mode.timing.h_total > 0 && mode.timing.v_total > 0
		&& mode.timing.pixel_clock > 0) {
		uint64_t refreshRate = (uint64_t)mode.timing.pixel_clock * 1000000
			/ mode.timing.h_total / mode.timing.v_total;
		if (refreshRate > 0 && refreshRate <= INT32_MAX)
			refresh = (int32)refreshRate;
	}

	float scale = desktop->VirtualScreen().ScreenScaleAt(0);
	if (scale < 0.1f)
		scale = 1.0f;

	wl_output_send_geometry(output, 0, 0, 0, 0, WL_OUTPUT_SUBPIXEL_UNKNOWN,
		"Vitruvian", "Virtual", WL_OUTPUT_TRANSFORM_NORMAL);
	wl_output_send_mode(output,
		WL_OUTPUT_MODE_CURRENT | WL_OUTPUT_MODE_PREFERRED,
		width, height, refresh);

	if (version >= 2)
		wl_output_send_scale(output, (int32)ceilf(scale));

	if (version >= 4)
		wl_output_send_name(output, "WL-1");

	if (version >= 4)
		wl_output_send_description(output, "Vitruvian Wayland Output");

	if (version >= 2)
		wl_output_send_done(output);
}




#endif // VOS_WAYLAND
