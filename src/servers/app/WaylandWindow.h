/*
 * Copyright 2026, carlos20260001. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * Wayland window bridge for app_server.
 *
 * Connects wlroots-koan Wayland surfaces to native Koan windows managed by
 * app_server. Implements the koan_window_callbacks interface and drains the
 * thread-safe command queue on the app_server thread.
 *
 * Threading:
 *  - Native Window/ServerWindow objects are only touched from app_server.
 *  - The Wayland dispatch thread pushes commands to the queue and posts a
 *    kMsgWaylandWake BMessage to app_server's looper (fWakeTarget). Wake-ups
 *    are coalesced: only one is in flight until app_server handles it.
 *  - app_server drains the queue in response to the wake-up message.
 */

#ifndef WAYLAND_WINDOW_H
#define WAYLAND_WINDOW_H

/* Message code the Wayland dispatch thread uses to wake app_server. */
static const uint32 kMsgWaylandWake = 'wlwk';
/* koan_test_v1 sync passing a window's input looper ("koan:serial"). */
static const uint32 kMsgWaylandTestFence = 'wltf';

#include <Locker.h>
#include <Message.h>
#include <Messenger.h>
#include <ObjectList.h>
#include <PortLink.h>
#include <Rect.h>
#include <Referenceable.h>
#include <String.h>
#include <Window.h>
#include <OS.h>

#include "koan_adapter.h"
#include "koan_frame.h"
#include "koan_queue.h"

class Desktop;
class Screen;
class ServerApp;
class ServerWindow;
class Window;
class ServerBitmap;

struct wlr_xdg_toplevel;

struct wayland_window {
	struct wl_list link;
	struct wlr_xdg_toplevel *toplevel;  /* key only: never dereferenced here */
	ServerWindow *server_window;        /* a WaylandServerWindow */
	::Window *window;
	bool visible;
	bool maximized;
	BRect restore_frame;                /* native frame before maximizing */
	bool fullscreen;
	BRect fullscreen_restore_frame;     /* native frame before fullscreen */
	window_look fullscreen_restore_look;
	int32 pid;                          /* client process, 0 if unknown */
	BMessenger input;                   /* the window's WaylandInputLooper */
	/* An xdg_popup's window: toplevel is then its root toplevel's key
	 * (input goes there, in its coordinates) and popup its own key. */
	bool is_popup;
	struct wlr_xdg_popup *popup;
	int32 popup_x, popup_y;             /* in the toplevel's content */
	/* The bitmap the window showed before its current one, kept to import
	 * the next frame of the same size into (process_set_buffer()). */
	BReference<ServerBitmap> spare_bitmap;
};

/* A Wayland client process with native windows, registered with the
 * registrar under its pid so Deskbar lists it like a native app. */
struct wayland_client {
	struct wl_list link;
	int32 pid;
	int32 windows;
	bool registered;
};

class WaylandWindowBridge {
public:
	WaylandWindowBridge();
	~WaylandWindowBridge();

	/* Initialise: called from app_server startup (app_server thread). */
	status_t Init();

	/* Set the desktop that owns Wayland windows. */
	/* Also registers the bridge's DesktopListener (popups follow their
	 * window; clicks outside every client close menus). */
	void SetDesktop(Desktop *desktop);

	/* Report the screen's display mode to the Koan wl_output, so clients
	 * see the real resolution and refresh rate. Called with the desktop's
	 * screen when it is set and after every mode change; it only queues
	 * a command, so any thread may call it. */
	void ScreenChanged(::Screen *screen);

	/* Set the looper the Wayland thread wakes when commands are queued.
	 * This is app_server itself; the bridge does not own it. A BMessenger
	 * is required: BLooper drops raw port writes without a flattened
	 * BMessage, so write_port(code, NULL, 0) never reaches the handler. */
	void SetWakeTarget(const BMessenger& target) { fWakeTarget = target; }

	/* Shut down: called from app_server shutdown (app_server thread). */
	void Shutdown();

	/* Start the Wayland dispatch thread. Returns the thread id. */
	thread_id StartDispatchThread();

	/* Stop the Wayland dispatch thread. */
	void StopDispatchThread();

	/* Drain pending commands from the queue. Called from app_server's
	 * message loop when a wake-up message is received. */
	void DrainCommands();

	/* Handle a wake-up message from the Wayland thread. */
	void WaylandWakeup();

	/* Access the Koan Wayland display (for WAYLAND_DISPLAY env var). */
	const char *SocketName() const { return fSocketName; }

	/* Queue a native window action (enum koan_native_action) for the
	 * app_server thread. Safe from any thread and from inside Desktop
	 * calls that hold window locks: the action runs when the queue is
	 * drained. */
	void QueueNativeAction(void *window, int32 action, int32 arg);

	/* koan_test_v1: the EventDispatcher passed a sync fence (a
	 * kEventStreamFence message); pass it through every input looper. */
	void TestFenceReached(BMessage *fence);

	/* Whether any popup window exists (any thread; the listener skips
	 * its queue pushes otherwise). */
	bool HasPopups() const { return atomic_get((int32 *)&fPopupCount) > 0; }

private:
	/* koan_window_callbacks implementations (called from Wayland thread,
	 * push commands to the queue). */
	static void s_create_window(void *userdata, struct koan_queue *queue,
			struct wlr_xdg_toplevel *toplevel, int32_t width,
			int32_t height);
	static void s_destroy_window(void *userdata, struct koan_queue *queue,
			void *window);
	static void s_set_title(void *userdata, struct koan_queue *queue,
			void *window, const char *title);
	static void s_set_size(void *userdata, struct koan_queue *queue,
			void *window, int32_t width, int32_t height);
	static void s_set_buffer(void *userdata, struct koan_queue *queue,
			void *window, void *data, int32_t width, int32_t height,
			int32_t stride, uint32_t format, float scale,
			int32_t acked_width, int32_t acked_height);
	static void s_set_visibility(void *userdata, struct koan_queue *queue,
			void *window, bool visible);
	static void s_request_state(void *userdata, struct koan_queue *queue,
			void *window, int32_t maximized, int32_t fullscreen);
	static void s_request_minimize(void *userdata, struct koan_queue *queue,
			void *window);
	static void s_wake_app_server(void *userdata);
	static void s_create_popup(void *userdata, struct koan_queue *queue,
			struct wlr_xdg_popup *popup, struct wlr_xdg_toplevel *toplevel,
			int32_t x, int32_t y, int32_t width, int32_t height);
	static void s_move_popup(void *userdata, struct koan_queue *queue,
			void *window, int32_t x, int32_t y);

	/* Command queue processors (called from app_server thread). */
	int process_create_window(struct koan_command *cmd);
	int process_destroy_window(struct koan_command *cmd);
	int process_set_title(struct koan_command *cmd);
	int process_set_size(struct koan_command *cmd);
	int process_set_buffer(struct koan_command *cmd);
	int process_set_visibility(struct koan_command *cmd);
	int process_native_action(struct koan_command *cmd);
	int process_create_popup(struct koan_command *cmd);
	int process_move_popup(struct koan_command *cmd);
	int process_test_place(struct koan_command *cmd);
	int process_test_pointer(struct koan_command *cmd);
	int process_test_fence(struct koan_command *cmd);
	void zoom_window(wayland_window *ww);
	void fullscreen_window(wayland_window *ww, bool enter);
	void configure_current_state(wayland_window *ww);

	/* Deskbar/registrar bookkeeping (app_server thread, fLock held). */
	void client_window_added(int32 pid, const char *app_id);
	void client_window_removed(int32 pid);
	void unregister_all_clients();

	/* Wayland dispatch thread entry point. A static member (rather than a
	 * free function) so it can access the private fDisplay member. */
	static int32 s_dispatch_thread(void *arg);

	static int s_queue_processor(struct koan_command *cmd, void *userdata);

	/* Post a coalesced wake-up to app_server (Wayland thread). */
	void _Wake();

	wayland_window *find_window_by_toplevel(struct wlr_xdg_toplevel *toplevel);
	wayland_window *find_window_by_server_window(ServerWindow *server_window);

	status_t EnsureServerApp();
	/* popupFrame non-NULL: an xdg_popup's window at that screen frame,
	 * whose input is offset by (offsetX, offsetY) into its toplevel's
	 * content coordinates. */
	status_t create_native_window(wayland_window *ww, const char *title,
			int32_t width, int32_t height, const BRect *popupFrame = NULL,
			int32 offsetX = 0, int32 offsetY = 0);
	void destroy_native_window(wayland_window *ww);
	void update_buffer(wayland_window *ww, void *data, int32_t width,
			int32_t height, int32_t stride, uint32_t format);
	void set_window_visibility(wayland_window *ww, bool visible);

	BLocker fLock;
	struct wl_list fWindows;      /* wayland_window.link */
	struct wl_list fClients;      /* wayland_client.link */
	BMessenger fWakeTarget;       /* app_server's looper (not owned) */
	int32 fWakePending;           /* 1 while a wake-up is in flight */
	thread_id fDispatchThread;    /* Wayland dispatch thread */
	bool fRunning;
	struct wl_display *fDisplay;
	Desktop *fDesktop;
	ServerApp *fServerApp;        /* shared client context for all windows */
	int32 fNextViewToken;         /* top-view tokens in fServerApp's space */
	BPoint fTestWhere;            /* koan_test_v1 pointer (app_server thread) */
	int32 fPopupCount;            /* popup windows (atomic) */
	class WaylandDesktopListener *fListener;
	void follow_popups(wayland_window *ww);
	int32 fTestButtons;
	char fSocketName[64];
};

extern WaylandWindowBridge *gWaylandWindowBridge;

#endif // WAYLAND_WINDOW_H
