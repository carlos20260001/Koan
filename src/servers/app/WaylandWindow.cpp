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
 *  - The Wayland dispatch thread pushes commands to the queue and writes a
 *    wake-up message to app_server's looper port (fWakePort).
 *  - app_server drains the queue in response to the wake-up message.
 */

#include "WaylandWindow.h"

#include <new>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <MessagePrivate.h>
#include <PortLink.h>

#include "AppServer.h"
#include "BitmapManager.h"
#include "Desktop.h"
#include "DrawingEngine.h"
#include "ServerApp.h"
#include "ServerBitmap.h"
#include "ServerWindow.h"
#include "Window.h"
#include "View.h"
#include "koan_server.h"

/* wlroots and libwayland declarations are C linkage: wlroots public headers
 * have no extern "C" guards of their own. */
extern "C" {
#include <wayland-server-core.h>
#include <wlr/types/wlr_xdg_shell.h>
#include <wlr/util/log.h>
}

#include <drm/drm_fourcc.h>

WaylandWindowBridge *gWaylandWindowBridge = NULL;

WaylandWindowBridge::WaylandWindowBridge()
	:
	fLock("WaylandWindowBridge lock"),
	fWakePort(-1),
	fDispatchThread(-1),
	fRunning(false),
	fDisplay(NULL),
	fDesktop(NULL),
	fServerApp(NULL)
{
	wl_list_init(&fWindows);
	memset(fSocketName, 0, sizeof(fSocketName));
}

WaylandWindowBridge::~WaylandWindowBridge()
{
	Shutdown();
}

status_t
WaylandWindowBridge::Init()
{
	/* Register the window-management callbacks with the adapter. The
	 * adapter runs on the Wayland dispatch thread and pushes commands to
	 * the queue; we drain them on the app_server thread.
	 *
	 * The command queue itself is created by koan_server_create() and
	 * passed to koan_adapter_init() when the dispatch thread starts. We
	 * pass NULL here; the real queue is set later. */
	static struct koan_window_callbacks cbs = {
		userdata: this,
		create_window: s_create_window,
		destroy_window: s_destroy_window,
		set_title: s_set_title,
		set_size: s_set_size,
		set_buffer: s_set_buffer,
		set_visibility: s_set_visibility,
	};
	koan_adapter_set_window_callbacks(&cbs, NULL, this);

	/* The wake port is app_server's own looper port; it is installed with
	 * SetWakePort() from AppServer before the dispatch thread starts.
	 * The bridge never creates or deletes it. */
	return B_OK;
}

void
WaylandWindowBridge::Shutdown()
{
	/* Terminate the Wayland event loop first. The dispatch thread blocks in
	 * wl_display_run(), so it must be stopped with wl_display_terminate()
	 * rather than the app_server wake port. */
	StopDispatchThread();

	if (fDisplay) {
		koan_server_destroy(fDisplay);
		fDisplay = NULL;
	}

	/* fWakePort belongs to app_server; just forget it. */
	fWakePort = -1;

	BAutolock locker(fLock);
	struct wayland_window *ww, *tmp;
	wl_list_for_each_safe(ww, tmp, &fWindows, link) {
		destroy_native_window(ww);
		delete ww;
	}
	wl_list_init(&fWindows);

	/* The shared client context outlives individual windows. Quit it
	 * after every ServerWindow is gone so its window list is empty, then
	 * wait for the message loop to exit before deleting it. */
	if (fServerApp) {
		fServerApp->Quit();
		thread_id thread = fServerApp->Thread();
		if (thread >= B_OK)
			wait_for_thread(thread, NULL);
		delete fServerApp;
		fServerApp = NULL;
	}
}

/* -------------------------------------------------------------------------- */
/* Dispatch thread                                                            */
/* -------------------------------------------------------------------------- */

int32
WaylandWindowBridge::s_dispatch_thread(void *arg)
{
	WaylandWindowBridge *bridge = (WaylandWindowBridge *)arg;

	wlr_log(WLR_INFO, "Wayland dispatch thread: starting");

	/* Run the Wayland event loop. This blocks until the display is
	 * terminated. */
	koan_server_run(bridge->fDisplay);

	wlr_log(WLR_INFO, "Wayland dispatch thread: exiting");
	return 0;
}

thread_id
WaylandWindowBridge::StartDispatchThread()
{
	fDisplay = koan_server_create();
	if (!fDisplay) {
		wlr_log(WLR_ERROR, "Wayland bridge: failed to create Wayland server");
		return -1;
	}

	const char *socket = koan_server_add_socket(fDisplay);
	if (!socket) {
		wlr_log(WLR_ERROR, "Wayland bridge: failed to add Wayland socket");
		koan_server_destroy(fDisplay);
		fDisplay = NULL;
		return -1;
	}

	strlcpy(fSocketName, socket, sizeof(fSocketName));

	if (!koan_server_start()) {
		wlr_log(WLR_ERROR, "Wayland bridge: failed to start backend");
		koan_server_destroy(fDisplay);
		fDisplay = NULL;
		fSocketName[0] = '\0';
		return -1;
	}

	fRunning = true;
	fDispatchThread = spawn_thread(s_dispatch_thread,
		"wayland dispatch", B_DISPLAY_PRIORITY, this);
	if (fDispatchThread < B_OK) {
		wlr_log(WLR_ERROR, "Wayland bridge: failed to spawn dispatch thread");
		fRunning = false;
		koan_server_destroy(fDisplay);
		fDisplay = NULL;
		fSocketName[0] = '\0';
		return fDispatchThread;
	}

	if (resume_thread(fDispatchThread) != B_OK) {
		wlr_log(WLR_ERROR, "Wayland bridge: failed to resume dispatch thread");
		kill_thread(fDispatchThread);
		fDispatchThread = -1;
		fRunning = false;
		koan_server_destroy(fDisplay);
		fDisplay = NULL;
		fSocketName[0] = '\0';
		return B_ERROR;
	}

	wlr_log(WLR_INFO, "Wayland bridge: dispatch thread started, socket %s",
		fSocketName);
	return fDispatchThread;
}

void
WaylandWindowBridge::StopDispatchThread()
{
	/* Terminate the display even when the thread never started: fDisplay
	 * may have been created before spawn_thread() failed. */
	if (fDisplay)
		wl_display_terminate(fDisplay);

	if (fDispatchThread >= B_OK) {
		wait_for_thread(fDispatchThread, NULL);
		fDispatchThread = -1;
	}

	fRunning = false;
}

/* -------------------------------------------------------------------------- */
/* Command draining (app_server thread)                                       */
/* -------------------------------------------------------------------------- */

void
WaylandWindowBridge::DrainCommands()
{
	struct koan_queue *queue = koan_adapter_queue();
	if (!queue)
		return;

	int processed = koan_queue_drain_direction(queue,
		KOAN_QUEUE_TO_APP_SERVER, s_queue_processor, this);
	if (processed > 0)
		wlr_log(WLR_INFO, "Wayland bridge: drained %d commands", processed);
}

void
WaylandWindowBridge::WaylandWakeup()
{
	/* app_server's message loop already consumed the wake-up message
	 * from its own port; all that is left is to process the commands. */
	DrainCommands();
}

/* -------------------------------------------------------------------------- */
/* koan_window_callbacks (Wayland thread -> queue)                            */
/* -------------------------------------------------------------------------- */

void
WaylandWindowBridge::s_create_window(void *userdata, struct koan_queue *queue,
		struct wlr_xdg_toplevel *toplevel, int32_t width, int32_t height)
{
	WaylandWindowBridge *bridge = (WaylandWindowBridge *)userdata;
	if (!bridge || !queue || !toplevel)
		return;

	union koan_cmd_payload payload = {0};
	payload.create_window.toplevel = toplevel;
	payload.create_window.width = width;
	payload.create_window.height = height;
	/* Copy the title while we are still on the Wayland thread. The
	 * toplevel and its title may be destroyed before app_server drains
	 * the command, so the receiver must not dereference it. */
	payload.create_window.title = toplevel->title
		? strdup(toplevel->title) : NULL;

	/* Push the command without blocking. app_server processes it on its
	 * thread and replies via KOAN_CMD_WINDOW_READY, which the Wayland
	 * thread drains through koan_adapter_drain_commands(). The queue
	 * deep-copies the title, so our copy is always ours to free. */
	struct koan_command *queued = koan_queue_push(queue,
		KOAN_CMD_CREATE_WINDOW, &payload, NULL);
	free(payload.create_window.title);
	if (!queued) {
		wlr_log(WLR_ERROR, "Wayland bridge: failed to queue CREATE_WINDOW");
		return;
	}

	/* Notify app_server that a command is pending. */
	if (bridge->fWakePort >= B_OK)
		write_port(bridge->fWakePort, kMsgWaylandWake, NULL, 0);
}

void
WaylandWindowBridge::s_destroy_window(void *userdata, struct koan_queue *queue,
		void *window)
{
	WaylandWindowBridge *bridge = (WaylandWindowBridge *)userdata;
	if (!bridge || !queue || !window)
		return;

	union koan_cmd_payload payload = {0};
	payload.destroy_window.window = window;
	koan_queue_push(queue, KOAN_CMD_DESTROY_WINDOW, &payload, NULL);

	if (bridge->fWakePort >= B_OK)
		write_port(bridge->fWakePort, kMsgWaylandWake, NULL, 0);
}

void
WaylandWindowBridge::s_set_title(void *userdata, struct koan_queue *queue,
		void *window, const char *title)
{
	WaylandWindowBridge *bridge = (WaylandWindowBridge *)userdata;
	if (!bridge || !queue || !window)
		return;

	union koan_cmd_payload payload = {0};
	payload.set_title.window = window;
	payload.set_title.title = strdup(title);
	koan_queue_push(queue, KOAN_CMD_SET_TITLE, &payload, NULL);
	/* dup_payload() made its own strdup() copy for the queue; free ours. */
	free((void *)payload.set_title.title);

	if (bridge->fWakePort >= B_OK)
		write_port(bridge->fWakePort, kMsgWaylandWake, NULL, 0);
}

void
WaylandWindowBridge::s_set_size(void *userdata, struct koan_queue *queue,
		void *window, int32_t width, int32_t height)
{
	WaylandWindowBridge *bridge = (WaylandWindowBridge *)userdata;
	if (!bridge || !queue || !window)
		return;

	union koan_cmd_payload payload = {0};
	payload.set_size.window = window;
	payload.set_size.width = width;
	payload.set_size.height = height;
	koan_queue_push(queue, KOAN_CMD_SET_SIZE, &payload, NULL);

	if (bridge->fWakePort >= B_OK)
		write_port(bridge->fWakePort, kMsgWaylandWake, NULL, 0);
}

void
WaylandWindowBridge::s_set_buffer(void *userdata, struct koan_queue *queue,
		void *window, void *data, int32_t width, int32_t height,
		int32_t stride, uint32_t format)
{
	WaylandWindowBridge *bridge = (WaylandWindowBridge *)userdata;
	if (!bridge || !queue || !window || !data)
		return;

	/* Copy the data so the queue owns it. The adapter already copied the
	 * wlroots pixels into 'data'; we copy again so the queue's copy is
	 * independent of the caller's buffer. */
	size_t size = (size_t)height * (size_t)stride;
	void *copy = malloc(size);
	if (!copy) {
		wlr_log(WLR_ERROR, "Wayland bridge: failed to allocate %zu bytes "
			"for buffer copy", size);
		free(data); /* caller's buffer - free it */
		return;
	}
	memcpy(copy, data, size);
	free(data); /* caller's buffer is no longer needed */

	union koan_cmd_payload payload = {0};
	payload.set_buffer.window = window;
	payload.set_buffer.data = copy;
	payload.set_buffer.width = width;
	payload.set_buffer.height = height;
	payload.set_buffer.stride = stride;
	payload.set_buffer.format = format;
	koan_queue_push(queue, KOAN_CMD_SET_BUFFER, &payload, NULL);

	if (bridge->fWakePort >= B_OK)
		write_port(bridge->fWakePort, kMsgWaylandWake, NULL, 0);
}

void
WaylandWindowBridge::s_set_visibility(void *userdata, struct koan_queue *queue,
		void *window, bool visible)
{
	WaylandWindowBridge *bridge = (WaylandWindowBridge *)userdata;
	if (!bridge || !queue || !window)
		return;

	union koan_cmd_payload payload = {0};
	payload.set_visibility.window = window;
	payload.set_visibility.visible = visible;
	koan_queue_push(queue, KOAN_CMD_SET_VISIBILITY, &payload, NULL);

	if (bridge->fWakePort >= B_OK)
		write_port(bridge->fWakePort, kMsgWaylandWake, NULL, 0);
}

/* -------------------------------------------------------------------------- */
/* Command processors (app_server thread)                                     */
/* -------------------------------------------------------------------------- */

int
WaylandWindowBridge::s_queue_processor(struct koan_command *cmd, void *userdata)
{
	WaylandWindowBridge *bridge = (WaylandWindowBridge *)userdata;
	if (!bridge || !cmd)
		return -1;

	switch (cmd->type) {
	case KOAN_CMD_CREATE_WINDOW:
		return bridge->process_create_window(cmd);
	case KOAN_CMD_DESTROY_WINDOW:
		return bridge->process_destroy_window(cmd);
	case KOAN_CMD_SET_TITLE:
		return bridge->process_set_title(cmd);
	case KOAN_CMD_SET_SIZE:
		return bridge->process_set_size(cmd);
	case KOAN_CMD_SET_BUFFER:
		return bridge->process_set_buffer(cmd);
	case KOAN_CMD_SET_VISIBILITY:
		return bridge->process_set_visibility(cmd);
	default:
		wlr_log(WLR_ERROR, "Wayland bridge: unknown command type %d",
			cmd->type);
		return -1;
	}
}

status_t
WaylandWindowBridge::EnsureServerApp()
{
	if (fServerApp)
		return B_OK;
	if (!fDesktop)
		return B_ERROR;

	/* A ServerWindow needs a ServerApp to own it. One shared context is
	 * enough for all Wayland windows; it is not registered in the
	 * Desktop's client application list, so the bridge owns it and
	 * quits/deletes it explicitly in Shutdown().
	 *
	 * The client ports are intentionally invalid: all Wayland traffic
	 * goes through this bridge, not through the BWindow message
	 * protocol. The client team is app_server itself so the ServerApp's
	 * message port is owned by a live team. */
	ServerApp *app = new(std::nothrow) ServerApp(fDesktop, -1, -1,
		getpid(), -1, "application/x-vnd.Vos-WaylandClient");
	if (!app)
		return B_NO_MEMORY;

	status_t status = app->Run();
	if (status != B_OK) {
		delete app;
		return status;
	}

	fServerApp = app;
	return B_OK;
}

status_t
WaylandWindowBridge::create_native_window(wayland_window *ww,
		const char *title, int32_t width, int32_t height)
{
	if (!fDesktop)
		return B_ERROR;

	status_t status = EnsureServerApp();
	if (status != B_OK)
		return status;

	ServerWindow *server_window = new(std::nothrow) ServerWindow(
		title && title[0] ? title : "Wayland Window",
		fServerApp, -1, -1, -1);
	if (!server_window)
		return B_NO_MEMORY;

	BRect frame(0, 0, width - 1, height - 1);
	status = server_window->Init(frame, B_BORDERED_WINDOW_LOOK,
		B_NORMAL_WINDOW_FEEL, B_NOT_RESIZABLE | B_NOT_MINIMIZABLE,
		B_CURRENT_WORKSPACE);
	if (status != B_OK) {
		/* Init() may already have added the window to the app's list;
		 * the destructor removes it again. */
		delete server_window;
		return status;
	}

	::Window *window = server_window->Window();
	if (!window) {
		/* Quit() deletes the ServerWindow synchronously because its
		 * message loop was never started, which also removes it from
		 * the desktop. */
		server_window->Quit();
		return B_ERROR;
	}

	/* ServerWindow::Init() already added the window to the desktop.
	 * Adding it a second time would corrupt the window list. */

	ww->server_window = server_window;
	ww->window = window;

	wlr_log(WLR_INFO, "Wayland bridge: created native window %dx%d", width,
		height);
	return B_OK;
}

void
WaylandWindowBridge::destroy_native_window(wayland_window *ww)
{
	if (!ww)
		return;

	/* The ServerWindow owns the native Window and removes it from the
	 * desktop in its destructor. Quit() deletes it synchronously since
	 * its message loop was never started. */
	if (ww->server_window) {
		ww->server_window->Quit();
		ww->server_window = NULL;
	}

	ww->window = NULL;
	ww->visible = false;
}

int
WaylandWindowBridge::process_create_window(struct koan_command *cmd)
{
	struct wlr_xdg_toplevel *toplevel = cmd->payload.create_window.toplevel;
	const char *title = cmd->payload.create_window.title;
	int32_t width = cmd->payload.create_window.width;
	int32_t height = cmd->payload.create_window.height;

	BAutolock locker(fLock);

	/* Don't create duplicates. */
	if (find_window_by_toplevel(toplevel)) {
		cmd->payload.create_window.window_out = NULL;
		return 0;
	}

	wayland_window *ww = new(std::nothrow) wayland_window;
	if (!ww) {
		cmd->payload.create_window.window_out = NULL;
		return -1;
	}
	ww->toplevel = toplevel;
	ww->server_window = NULL;
	ww->window = NULL;
	ww->visible = false;

	status_t status = create_native_window(ww, title, width, height);
	if (status != B_OK) {
		delete ww;
		cmd->payload.create_window.window_out = NULL;
		return -1;
	}

	wl_list_insert(&fWindows, &ww->link);
	cmd->payload.create_window.window_out = ww;

	/* CREATE_WINDOW is only queued once the toplevel is mapped, so the
	 * native window starts visible. Re-maps are handled by the adapter's
	 * SET_VISIBILITY command. */
	if (fDesktop && ww->window && !ww->visible) {
		fDesktop->ShowWindow(ww->window);
		ww->visible = true;
	}

	/* Notify the Wayland thread that the native window handle is ready
	 * by pushing KOAN_CMD_WINDOW_READY back onto the queue. The push
	 * wakes the Wayland event loop through the queue's wake pipe. */
	union koan_cmd_payload ready_payload = {0};
	ready_payload.window_ready.toplevel = toplevel;
	ready_payload.window_ready.window = ww;
	koan_queue_push(koan_adapter_queue(), KOAN_CMD_WINDOW_READY,
		&ready_payload, NULL);

	return 0;
}

int
WaylandWindowBridge::process_destroy_window(struct koan_command *cmd)
{
	void *window = cmd->payload.destroy_window.window;

	BAutolock locker(fLock);

	/* The destroy_window callback passes the native_window handle (which
	 * is the wayland_window* stored as window_out). */
	struct wayland_window *ww = NULL;
	struct wayland_window *w;
	wl_list_for_each(w, &fWindows, link) {
		if (w == window) {
			ww = w;
			break;
		}
	}

	if (!ww)
		return 0;

	wl_list_remove(&ww->link);
	destroy_native_window(ww);
	delete ww;
	return 0;
}

int
WaylandWindowBridge::process_set_title(struct koan_command *cmd)
{
	void *window = cmd->payload.set_title.window;
	const char *title = cmd->payload.set_title.title;

	BAutolock locker(fLock);

	wayland_window *ww = (wayland_window *)window;
	if (!ww || !ww->window || !fDesktop)
		return 0;

	fDesktop->SetWindowTitle(ww->window, title);
	/* Do NOT free 'title' here: koan_queue_drain_direction() calls
	 * free_payload() which frees set_title.title after the processor
	 * returns. */
	return 0;
}

int
WaylandWindowBridge::process_set_size(struct koan_command *cmd)
{
	(void)cmd;
	/* Window resizing via xdg_toplevel is a client request; the compositor
	 * should send a configure. Not yet implemented. */
	return 0;
}

static color_space
drm_format_to_colorspace(uint32_t format)
{
	switch (format) {
	case DRM_FORMAT_ARGB8888:
		return B_RGBA32;
	case DRM_FORMAT_XRGB8888:
		return B_RGB32;
	case DRM_FORMAT_XBGR8888:
		return B_RGB32;
	case DRM_FORMAT_ABGR8888:
		return B_RGBA32;
	default:
		return B_RGBA32;
	}
}

int
WaylandWindowBridge::process_set_buffer(struct koan_command *cmd)
{
	void *window = cmd->payload.set_buffer.window;
	void *data = cmd->payload.set_buffer.data;
	int32_t width = cmd->payload.set_buffer.width;
	int32_t height = cmd->payload.set_buffer.height;
	int32_t stride = cmd->payload.set_buffer.stride;
	uint32_t format = cmd->payload.set_buffer.format;

	BAutolock locker(fLock);

	wayland_window *ww = (wayland_window *)window;
	if (!ww || !ww->window || !ww->server_window)
		return 0;

	::Window *native_window = ww->window;
	View *top_view = native_window->TopView();
	if (!top_view)
		return 0;

	color_space space = drm_format_to_colorspace(format);

	/* CreateBitmap() returns an owned reference (count 1) and registers the
	 * bitmap with the manager using a raw pointer. Adopt that reference so
	 * both the manager registration and the reference are released on every
	 * path below. */
	BReference<ServerBitmap> bitmap(gBitmapManager->CreateBitmap(
		NULL, *ww->server_window->Desktop()->HWInterface(),
		BRect(0, 0, width - 1, height - 1), space, 0, stride), true);
	if (!bitmap.IsSet()) {
		return -1;
	}

	status_t status = bitmap->ImportBits(data, stride * height, stride,
		space);
	/* Do NOT free 'data' here: koan_queue_drain_direction() calls
	 * free_payload() which frees set_buffer.data after the processor
	 * returns. */

	if (status != B_OK) {
		/* Detach from the manager before the reference is dropped: it
		 * holds a raw pointer and would otherwise keep a dangling entry. */
		gBitmapManager->BitmapRemoved(bitmap);
		return -1;
	}

	/* The view takes its own reference. The bitmap is not client-visible,
	 * so drop the manager registration and our creation reference; the
	 * view owns it until it is replaced or destroyed. */
	top_view->SetViewBitmap(bitmap, IntRect(0, 0, width - 1, height - 1),
		IntRect(0, 0, width - 1, height - 1), 0 /* no tiling */, 0);
	gBitmapManager->BitmapRemoved(bitmap);

	native_window->MarkContentDirty(native_window->VisibleRegion(),
		native_window->VisibleRegion());

	return 0;
}

int
WaylandWindowBridge::process_set_visibility(struct koan_command *cmd)
{
	void *window = cmd->payload.set_visibility.window;
	bool visible = cmd->payload.set_visibility.visible;

	BAutolock locker(fLock);

	wayland_window *ww = (wayland_window *)window;
	if (!ww || !ww->window || !fDesktop)
		return 0;

	if (visible && !ww->visible) {
		fDesktop->ShowWindow(ww->window);
		ww->visible = true;
	} else if (!visible && ww->visible) {
		fDesktop->HideWindow(ww->window);
		ww->visible = false;
	}

	return 0;
}

wayland_window *
WaylandWindowBridge::find_window_by_toplevel(struct wlr_xdg_toplevel *toplevel)
{
	struct wayland_window *w;
	wl_list_for_each(w, &fWindows, link) {
		if (w->toplevel == toplevel)
			return w;
	}
	return NULL;
}

wayland_window *
WaylandWindowBridge::find_window_by_server_window(ServerWindow *server_window)
{
	struct wayland_window *w;
	wl_list_for_each(w, &fWindows, link) {
		if (w->server_window == server_window)
			return w;
	}
	return NULL;
}
