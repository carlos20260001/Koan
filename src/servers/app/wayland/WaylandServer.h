/*
 * Copyright 2026, Koan contributors
 * SPDX-License-Identifier: Apache-2.0
 *
 * Part of Koan, a VitruvianOS fork
 */


#ifndef VITRUVIAN_WAYLAND_SERVER_H
#define VITRUVIAN_WAYLAND_SERVER_H


#include <Locker.h>
#include <String.h>
#include <Thread.h>
#include <Rect.h>
#include <Region.h>
#include <ObjectList.h>
#include <GraphicsDefs.h>
#include <AffineTransform.h>

#ifdef VOS_WAYLAND
#include <wayland-server.h>
#include <wayland-server-core.h>
#include <stdio.h>
#endif


class Desktop;
class Window;
class ServerWindow;
class DrawingEngine;
class ServerBitmap;
class WaylandServer;


/*! Wayland surface tracked by the server.

    Maps a wl_surface resource to pixel data (from wl_shm buffers) and
    optionally to a Vitruvian Window. This is the bridge between the
    Wayland protocol world and the native app_server windowing model.
 */
struct PoolData;

struct WaylandSurface {
								WaylandSurface(struct wl_resource* resource);
	virtual						~WaylandSurface();

			void				AcquireReference();
			void				ReleaseReference();

			struct wl_resource*	wlSurface;
			struct wl_resource*	xdgSurface;
			struct wl_resource*	attachedBuffer;
			WaylandSurface*		attachedBufferSurface;
#ifdef VOS_WAYLAND
			struct wl_listener	attachedBufferDestroyListener;
#endif
			WaylandServer*		server;          // owning server
			struct PoolData*	pool;            // shm pool backing bitmapData
			int32				referenceCount;  // resources holding this surface

			// Cached bitmap data (copy of client's shm buffer)
			void*				bitmapData;
			int32				bitmapSize;
			int32				bitmapCapacity;
			int32				bitmapOffset;
			int32				bitmapWidth;
			int32				bitmapHeight;
			int32				bitmapStride;
			color_space			bitmapFormat;
			bool				bitmapValid;
			bool				bitmapDataOwned;  // true if we malloc'd it (must free)

			BRect				frame;           // logical frame in screen coords
			bool				mapped;          // surface has an xdg_toplevel
			Window*				window;          // mapped Vitruvian window (or NULL)
			ServerWindow*		serverWindow;    // associated ServerWindow (or NULL)
			BRegion				dirty;           // accumulated damage region (logical coords)
			float				opacity;         // per-surface opacity (1.0 = opaque)
			int32				scale;           // buffer scale factor (default 1)
			BAffineTransform	transform;       // per-surface transform
};


/*! Minimal Wayland display server running inside app_server.

    Provides wl_display with core globals (wl_compositor, wl_shm, wl_seat,
    xdg_wm_base) so Wayland clients can connect. Each Wayland surface maps
    to a Vitruvian Window via Desktop.

    Runs on its own dispatch thread; Desktop owns the lifetime.

    Only compiled when VOS_WAYLAND is defined (wayland-server/client dev
    packages available).
 */
class WaylandServer {
public:
								WaylandServer(Desktop* desktop);
	virtual						~WaylandServer();

#ifdef VOS_WAYLAND
			status_t			InitCheck() const
									{ return fInitStatus; }
			Desktop*			GetDesktop() const { return fDesktop; }

			bool				Run();          // blocking dispatch loop
			void				Stop();         // request shutdown

			wl_display*			Display() const { return fDisplay; }
			const BString&		SocketName() const { return fSocketName; }

			// Surface enumeration for Desktop / window creation
			int32				CountSurfaces() const;
			WaylandSurface*		SurfaceAt(int32 index) const;
			WaylandSurface*		FindSurface(struct wl_resource* resource) const;
			void				AddSurface(WaylandSurface* surface);
			void				RemoveSurface(WaylandSurface* surface);

			// Paint all Wayland surfaces onto the screen
			status_t			PaintSurfaces(DrawingEngine* engine,
											const BRegion& dirtyRegion);

			// Input sending (called from Desktop to forward input to clients)
			void				SendPointerMotion(int32 x, int32 y,
													uint32 time);
			void				SendPointerButton(uint32 button,
													uint32 state,
													uint32 time);
			void				SendKeyboardKey(uint32 key,
													uint32 state,
													uint32 time,
													uint32 modifiers);

			// Surface property setters
			void				SetOpacity(WaylandSurface* surface,
											float opacity);
			void				SetTransform(WaylandSurface* surface,
											const BAffineTransform& transform);

			// Guards surface list and cached bitmap lifetime. The lock is
			// recursive, so RemoveSurface() may be called while held.
			void				LockSurfaces() { fLock.Lock(); }
			void				UnlockSurfaces() { fLock.Unlock(); }

			// Input resource tracking (used by protocol handlers)
			void				AddPointerResource(struct wl_resource* resource);
			void				RemovePointerResource(struct wl_resource* resource);
			void				AddKeyboardResource(struct wl_resource* resource);
			void				RemoveKeyboardResource(struct wl_resource* resource);

			// Helpers for protocol handlers, which are file-static
			void				MarkDirty(const BRegion& region);
			Window*				CreateNativeWindow(const BRect& frame,
									ServerWindow** serverWindow);
			void				DestroyNativeWindow(Window* window);
			void				SetNativeWindowTitle(Window* window,
									const char* title);
			void				MinimizeNativeWindow(Window* window);
			void				ForgetBuffer(struct wl_resource* buffer);
#endif // VOS_WAYLAND

private:
			Desktop*			fDesktop;
#ifdef VOS_WAYLAND
			status_t			fInitStatus;
			wl_display*			fDisplay;
			wl_protocol_logger*	fProtocolLogger;
			FILE*				fProtocolLog;
			BString				fSocketName;
			int32				fSocketFd;
			thread_id			fDispatchThread;
			BLocker				fLock;
			volatile bool		fRunning;

			BObjectList<WaylandSurface> fSurfaces;

			int32				fPointerX;
			int32				fPointerY;
			uint32				fLastSerial;
			struct wl_resource*	fFocusSurface;

			// Bound pointer and keyboard resources (for input forwarding)
			BObjectList<struct wl_resource, false> fPointerResources;
			BObjectList<struct wl_resource, false> fKeyboardResources;

			static int32		_DispatchThreadEntry(void* arg);
			void				_DispatchThread();

			// Global object handlers
			static void			_CompositorDestroy(struct wl_resource* resource);
			static void			_CompositorBind(struct wl_client* client,
									void* data, uint32_t version,
									uint32_t id);
			static void			_CompositorCreateSurface(
									struct wl_client* client,
									struct wl_resource* resource,
									uint32_t id);
			static void			_CompositorCreateRegion(
									struct wl_client* client,
									struct wl_resource* resource,
									uint32_t id);

			// Surface lifecycle
			static void			_SurfaceDestroy(struct wl_resource* resource);
			static void			_SurfaceAttach(
									struct wl_client* client,
									struct wl_resource* resource,
									struct wl_resource* buffer,
									int32_t x, int32_t y);
			static void			_SurfaceDamage(
									struct wl_client* client,
									struct wl_resource* resource,
									int32_t x, int32_t y,
									int32_t width, int32_t height);
			static void			_SurfaceCommit(
									struct wl_client* client,
									struct wl_resource* resource);

			// Region
			static void			_RegionDestroy(struct wl_resource* resource);
			static void			_RegionAdd(
									struct wl_client* client,
									struct wl_resource* resource,
									int32_t x, int32_t y,
									int32_t width, int32_t height);
			static void			_RegionSubtract(
									struct wl_client* client,
									struct wl_resource* resource,
									int32_t x, int32_t y,
									int32_t width, int32_t height);

			// shm
			static void			_ShmDestroy(struct wl_resource* resource);
			static void			_ShmBind(struct wl_client* client,
									void* data, uint32_t version,
									uint32_t id);
			static void			_ShmCreatePool(
									struct wl_client* client,
									struct wl_resource* resource,
									uint32_t id, int32_t fd,
									uint32_t size);
			static void			_ShmGetBuffer(
									struct wl_client* client,
									struct wl_resource* resource,
									uint32_t id, struct wl_resource* pool,
									uint32_t offset, uint32_t width,
									uint32_t height, uint32_t stride,
									uint32_t format);
			static void			_BufferDestroy(struct wl_resource* resource);

			// seat
			static void			_SeatDestroy(struct wl_resource* resource);
			static void			_SeatBind(struct wl_client* client,
									void* data, uint32_t version,
									uint32_t id);
			static void			_SeatGetPointer(
									struct wl_client* client,
									struct wl_resource* resource,
									uint32_t id);
			static void			_SeatGetKeyboard(
									struct wl_client* client,
									struct wl_resource* resource,
									uint32_t id);
			static void			_SeatGetTouch(
									struct wl_client* client,
									struct wl_resource* resource,
									uint32_t id);
			static void			_SeatRelease(struct wl_resource* resource);

			// pointer
			static void			_PointerDestroy(struct wl_resource* resource);
			static void			_PointerRelease(
									struct wl_client* client,
									struct wl_resource* resource);
			static void			_PointerSetCursor(
									struct wl_client* client,
									struct wl_resource* resource,
									uint32_t serial,
									struct wl_resource* surface,
									int32_t hotspot_x, int32_t hotspot_y);

			// keyboard
			static void			_KeyboardDestroy(struct wl_resource* resource);
			static void			_KeyboardRelease(
									struct wl_client* client,
									struct wl_resource* resource);

			// xdg_wm_base
			static void			_XdgWmBaseBind(struct wl_client* client,
									void* data, uint32_t version,
									uint32_t id);
			static void			_XdgWmBaseDestroy(struct wl_resource* resource);
			static void			_XdgWmBaseGetXdgSurface(
									struct wl_client* client,
									struct wl_resource* resource,
									uint32_t id,
									struct wl_resource* surface);
			static void			_XdgWmBasePong(
									struct wl_client* client,
									struct wl_resource* resource,
									uint32_t serial);

			// xdg_surface
			static void			_XdgSurfaceDestroy(struct wl_resource* resource);
			static void			_XdgSurfaceGetToplevel(
									struct wl_client* client,
									struct wl_resource* resource,
									uint32_t id);
			static void			_XdgSurfaceSetWindowGeometry(
									struct wl_client* client,
									struct wl_resource* resource,
									int32_t x, int32_t y,
									int32_t width, int32_t height);

			// xdg_toplevel
			static void			_XdgToplevelDestroy(struct wl_resource* resource);
			static void			_XdgToplevelSetParent(
									struct wl_client* client,
									struct wl_resource* resource,
									struct wl_resource* parent);
			static void			_XdgToplevelSetTitle(
									struct wl_client* client,
									struct wl_resource* resource,
									const char* title);
			static void			_XdgToplevelSetAppId(
									struct wl_client* client,
									struct wl_resource* resource,
									const char* app_id);
			static void			_XdgToplevelShowWindowMenu(
									struct wl_client* client,
									struct wl_resource* resource,
									uint32_t serial,
									struct wl_resource* seat,
									uint32_t x, int32_t y);
			static void			_XdgToplevelMove(
									struct wl_client* client,
									struct wl_resource* resource,
									struct wl_resource* seat,
									uint32_t serial);
			static void			_XdgToplevelResize(
									struct wl_client* client,
									struct wl_resource* resource,
									struct wl_resource* seat,
									uint32_t serial, uint32_t edges);
			static void			_XdgToplevelSetMaxSize(
									struct wl_client* client,
									struct wl_resource* resource,
									int32_t width, int32_t height);
			static void			_XdgToplevelSetMinSize(
									struct wl_client* client,
									struct wl_resource* resource,
									int32_t width, int32_t height);
			static void			_XdgToplevelSetMaximized(
									struct wl_client* client,
									struct wl_resource* resource);
			static void			_XdgToplevelUnsetMaximized(
									struct wl_client* client,
									struct wl_resource* resource);
			static void			_XdgToplevelSetMinimized(
									struct wl_client* client,
									struct wl_resource* resource);
			static void			_XdgToplevelSetFullscreen(
									struct wl_client* client,
									struct wl_resource* resource,
									struct wl_resource* output);
			static void			_XdgToplevelUnsetFullscreen(
									struct wl_client* client,
									struct wl_resource* resource);

			// Interface definitions
			static const struct wl_compositor_interface sCompositorInterface;
			static const struct wl_subcompositor_interface sSubcompositorInterface;
			static const struct wl_shm_interface sShmInterface;
			static const struct wl_seat_interface sSeatInterface;
			static const struct wl_pointer_interface sPointerInterface;
			static const struct wl_keyboard_interface sKeyboardInterface;
			static const struct xdg_wm_base_interface sXdgWmBaseInterface;
			static const struct xdg_surface_interface sXdgSurfaceInterface;
			static const struct xdg_toplevel_interface sXdgToplevelInterface;
#endif // VOS_WAYLAND
};


#endif	// WAYLAND_SERVER_H
