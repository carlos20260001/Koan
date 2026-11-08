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

#include <Locker.h>
#include <Message.h>
#include <Messenger.h>
#include <ObjectList.h>
#include <PortLink.h>
#include <Rect.h>
#include <String.h>
#include <OS.h>

#include "koan_adapter.h"
#include "koan_queue.h"

class Desktop;
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
	int32 pid;                          /* client process, 0 if unknown */
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
	void SetDesktop(Desktop *desktop) { fDesktop = desktop; }

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
			int32_t stride, uint32_t format);
	static void s_set_visibility(void *userdata, struct koan_queue *queue,
			void *window, bool visible);

	/* Command queue processors (called from app_server thread). */
	int process_create_window(struct koan_command *cmd);
	int process_destroy_window(struct koan_command *cmd);
	int process_set_title(struct koan_command *cmd);
	int process_set_size(struct koan_command *cmd);
	int process_set_buffer(struct koan_command *cmd);
	int process_set_visibility(struct koan_command *cmd);
	int process_native_action(struct koan_command *cmd);
	void zoom_window(wayland_window *ww);

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
	status_t create_native_window(wayland_window *ww, const char *title,
			int32_t width, int32_t height);
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
	char fSocketName[64];
};

extern WaylandWindowBridge *gWaylandWindowBridge;

#endif // WAYLAND_WINDOW_H
