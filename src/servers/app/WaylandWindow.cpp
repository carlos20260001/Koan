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
 *  - The Wayland dispatch thread pushes commands to the queue and writes to
 *    fWakePort to notify app_server.
 *  - app_server drains the queue in response to the wake-up message.
 */

#include "WaylandWindow.h"

#include <new>
#include <stdio.h>
#include <string.h>

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

#include <wlr/types/wlr_xdg_shell.h>
#include <wayland-server-core.h>
#include <wlr/util/log.h>
#include <drm/drm_fourcc.h>

WaylandWindowBridge *gWaylandWindowBridge = NULL;

WaylandWindowBridge::WaylandWindowBridge()
	:
	fLock("WaylandWindowBridge lock"),
	fWakePort(-1),
	fDispatchThread(-1),
	fRunning(false),
	fDisplay(NULL),
	fDesktop(NULL)
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

	/* Create a port the Wayland dispatch thread can write to in order to
	 * wake app_server's message loop. */
	fWakePort = create_port(16, "wayland_bridge");
	if (fWakePort < B_OK) {
		wlr_log(WLR_ERROR, "Wayland bridge: failed to create wake port");
		koan_queue_destroy(queue);
		return fWakePort;
	}

	return B_OK;
}

void
WaylandWindowBridge::Shutdown()
{
	if (fDispatchThread >= B_OK) {
		fRunning = false;
		if (fWakePort >= B_OK) {
			/* Wake the dispatch thread so it can exit. */
			write_port(fWakePort, kMsgWaylandWake, NULL, 0);
		}
		wait_for_thread(fDispatchThread, NULL);
		fDispatchThread = -1;
	}

	if (fWakePort >= B_OK) {
		delete_port(fWakePort);
		fWakePort = -1;
	}

	BAutolock locker(fLock);
	struct wayland_window *ww, *tmp;
	wl_list_for_each_safe(ww, tmp, &fWindows, link) {
		destroy_native_window(ww);
		delete ww;
	}
	wl_list_init(&fWindows);
}

/* -------------------------------------------------------------------------- */
/* Dispatch thread                                                            */
/* -------------------------------------------------------------------------- */

static int32
wayland_dispatch_thread(void *arg)
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
		return -1;
	}

	fRunning = true;
	fDispatchThread = spawn_thread(wayland_dispatch_thread,
		"wayland dispatch", B_DISPLAY_PRIORITY, this);
	if (fDispatchThread < B_OK) {
		fRunning = false;
		return fDispatchThread;
	}

	if (resume_thread(fDispatchThread) != B_OK) {
		kill_thread(fDispatchThread);
		fDispatchThread = -1;
		fRunning = false;
		return B_ERROR;
	}

	wlr_log(WLR_INFO, "Wayland bridge: dispatch thread started, socket %s",
		fSocketName);
	return fDispatchThread;
}

void
WaylandWindowBridge::StopDispatchThread()
{
	if (!fRunning)
		return;

	/* Terminate the Wayland display. This causes wl_display_run() to
	 * return and the dispatch thread to exit. */
	if (fDisplay) {
		wl_display_terminate(fDisplay);
	}

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

	int processed = koan_queue_drain(queue, s_queue_processor, this);
	if (processed > 0)
		wlr_log(WLR_INFO, "Wayland bridge: drained %d commands", processed);
}

void
WaylandWindowBridge::WaylandWakeup()
{
	/* Drain all pending messages from the wake port, then drain commands. */
	if (fWakePort < B_OK)
		return;

	while (true) {
		int32 code;
		ssize_t status = read_port(fWakePort, &code, NULL, 0);
		if (status < B_OK)
			break;
	}

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

	/* Push the command without blocking. app_server processes it on its
	 * thread and replies via KOAN_CMD_WINDOW_READY, which the Wayland
	 * thread drains through koan_adapter_drain_commands(). */
	if (!koan_queue_push(queue, KOAN_CMD_CREATE_WINDOW, &payload, NULL)) {
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
WaylandWindowBridge::create_native_window(wayland_window *ww,
		int32_t width, int32_t height)
{
	if (!fDesktop)
		return B_ERROR;

	/* We need a ServerApp to create a ServerWindow. Create a dedicated
	 * app_server client context for Wayland windows. */
	ServerApp *app = new(std::nothrow) ServerApp(fDesktop, -1, -1,
		B_SYSTEM_TEAM, -1, "application/x-vnd.Vos-WaylandClient");
	if (!app)
		return B_NO_MEMORY;

	ServerWindow *server_window = new(std::nothrow) ServerWindow(
		ww->toplevel->title ? ww->toplevel->title : "Wayland Window",
		app, -1, -1, -1);
	if (!server_window) {
		delete app;
		return B_NO_MEMORY;
	}

	BRect frame(0, 0, width - 1, height - 1);
	status_t status = server_window->Init(frame, B_BORDERED_WINDOW_LOOK,
		B_NORMAL_WINDOW_FEEL, B_NOT_RESIZABLE | B_NOT_MINIMIZABLE,
		B_CURRENT_WORKSPACE);
	if (status != B_OK) {
		delete server_window;
		delete app;
		return status;
	}

	::Window *window = server_window->Window();
	if (!window) {
		server_window->Quit();
		delete app;
		return B_ERROR;
	}

	fDesktop->AddWindow(window);

	ww->server_window = server_window;
	ww->window = window;
	ww->visible = true;

	wlr_log(WLR_INFO, "Wayland bridge: created native window %dx%d", width,
		height);
	return B_OK;
}

void
WaylandWindowBridge::destroy_native_window(wayland_window *ww)
{
	if (!ww)
		return;

	if (fDesktop && ww->window) {
		fDesktop->RemoveWindow(ww->window);
	}

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

	status_t status = create_native_window(ww, width, height);
	if (status != B_OK) {
		delete ww;
		cmd->payload.create_window.window_out = NULL;
		return -1;
	}

	wl_list_insert(&fWindows, &ww->link);
	cmd->payload.create_window.window_out = ww;

	/* Notify the Wayland thread that the native window handle is ready
	 * by pushing KOAN_CMD_WINDOW_READY back onto the queue. The Wayland
	 * thread's event loop will wake up and drain it. */
	union koan_cmd_payload ready_payload = {0};
	ready_payload.window_ready.toplevel = toplevel;
	ready_payload.window_ready.window = ww;
	koan_queue_push(koan_adapter_queue(), KOAN_CMD_WINDOW_READY,
		&ready_payload, NULL);

	/* Notify app_server that a command is pending. */
	if (fWakePort >= B_OK)
		write_port(fWakePort, kMsgWaylandWake, NULL, 0);

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
	/* Do NOT free 'title' here: koan_queue_drain() calls free_payload()
	 * which frees set_title.title after the processor returns. */
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

	ServerBitmap *bitmap = gBitmapManager->CreateBitmap(
		NULL, *ww->server_window->Desktop()->HWInterface(),
		BRect(0, 0, width - 1, height - 1), space, 0, stride);
	if (!bitmap) {
		return -1;
	}

	status_t status = bitmap->ImportBits(data, stride * height, stride,
		space);
	/* Do NOT free 'data' here: koan_queue_drain() calls free_payload()
	 * which frees set_buffer.data after the processor returns. */

	if (status != B_OK) {
		delete bitmap;
		return -1;
	}

	top_view->SetViewBitmap(bitmap, IntRect(0, 0, width - 1, height - 1),
		IntRect(0, 0, width - 1, height - 1), B_BITMAP_RESIZING_MODE_NONE, 0);

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
