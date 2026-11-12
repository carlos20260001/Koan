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
 *    coalesced kMsgWaylandWake BMessage to app_server's looper.
 *  - app_server drains the queue in response to the wake-up message.
 */

#include "WaylandWindow.h"

#include <new>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <ctype.h>
#include <limits.h>
#include <math.h>

#include <Entry.h>
#include <Looper.h>
#include <MessagePrivate.h>
#include <MessengerPrivate.h>
#include <PortLink.h>
#include <Roster.h>
#include <RosterPrivate.h>
#include <WindowPrivate.h>

#include "AppServer.h"
#include "BitmapManager.h"
#include "Desktop.h"
#include "DesktopListener.h"
#include "Decorator.h"
#include "DrawState.h"
#include "DrawingEngine.h"
#include "ServerApp.h"
#include "ServerBitmap.h"
#include "Screen.h"
#include "ServerWindow.h"
#include "Window.h"
#include "View.h"
#include "VirtualScreen.h"
#include "koan_server.h"
#include "LinuxKeycodeMap.h"

/* wlroots and libwayland declarations are C linkage: wlroots public headers
 * have no extern "C" guards of their own. */
extern "C" {
#include <wayland-server-core.h>
#include <wlr/types/wlr_xdg_shell.h>
#include <wlr/util/log.h>
}

#include <drm/drm_fourcc.h>

WaylandWindowBridge *gWaylandWindowBridge = NULL;

/* Queue a command for the Wayland thread; the push wakes it. */
static void
push_to_wayland(enum koan_cmd_type type, const union koan_cmd_payload &payload)
{
	struct koan_queue *queue = koan_adapter_queue();
	if (queue)
		koan_queue_push(queue, type, &payload, NULL);
}

/* -------------------------------------------------------------------------- */
/* WaylandInputLooper                                                         */
/* -------------------------------------------------------------------------- */

/* Haiku keycode -> Linux evdev code: the inverse of input_server's table
 * (LinuxKeycodeMap.h), so both directions stay one source of truth. */
static uint8 sHaikuToEvdev[256];
static pthread_once_t sHaikuToEvdevOnce = PTHREAD_ONCE_INIT;

static void
init_haiku_to_evdev()
{
	for (int evdev = 255; evdev > 0; evdev--) {
		uint8 haiku = kEvdevToHaiku[evdev];
		if (haiku != 0)
			sHaikuToEvdev[haiku] = (uint8)evdev;  /* lowest evdev code wins */
	}
}

static uint32
haiku_to_evdev(int32 key)
{
	pthread_once(&sHaikuToEvdevOnce, init_haiku_to_evdev);
	return key > 0 && key < 256 ? sHaikuToEvdev[key] : 0;
}

/* The "client" of a Wayland window. app_server's EventDispatcher delivers
 * a window's input to its client looper port, as for any BWindow; for a
 * Wayland toplevel that port belongs to this looper, which turns the
 * messages into wl_seat events for the Wayland client:
 *
 *  B_MOUSE_MOVED/DOWN/UP ("screen_where", "buttons") -> motion, buttons,
 *      leave (outside the content while no button is held)
 *  B_MOUSE_WHEEL_CHANGED ("be:wheel_delta_x/y")      -> axis
 *  B_KEY_DOWN/UP, B_UNMAPPED_KEY_DOWN/UP ("key")     -> evdev keys; every
 *      key, modifiers included, arrives this way, so xkb in the Wayland
 *      server derives the modifier state. Repeats are dropped: Wayland
 *      clients repeat on their own (repeat info on wl_keyboard).
 *
 * Like a BWindow it learns its frame from B_WINDOW_MOVED/RESIZED (see
 * WaylandServerWindow) and converts screen positions itself. */
class WaylandInputLooper : public BLooper {
public:
	/* popup: the looper of an xdg_popup's window. Its positions are
	 * offset into the toplevel's content coordinates (SetContentOffset),
	 * where Koan's hit-test finds the popup surface on top. */
	WaylandInputLooper(struct wlr_xdg_toplevel *toplevel, bool popup = false)
		:
		BLooper("wayland input"),
		fToplevel(toplevel),
		fLeft(0), fTop(0), fWidth(0), fHeight(0),
		fOffsetX(0), fOffsetY(0),
		fButtons(0),
		fInside(false),
		fPopup(popup)
	{
	}

	void SetContentOffset(int32 x, int32 y)
	{
		atomic_set(&fOffsetX, x);
		atomic_set(&fOffsetY, y);
	}

	port_id Port()
	{
		BMessenger messenger(this);
		return BMessenger::Private(messenger).Port();
	}

	void SetOrigin(int32 left, int32 top)
	{
		atomic_set(&fLeft, left);
		atomic_set(&fTop, top);
	}

	void SetSize(int32 width, int32 height)
	{
		atomic_set(&fWidth, width);
		atomic_set(&fHeight, height);
	}

	virtual void MessageReceived(BMessage *message)
	{
		switch (message->what) {
			case B_MOUSE_MOVED:
			case B_MOUSE_DOWN:
			case B_MOUSE_UP:
				_Mouse(message);
				break;

			case kMsgWaylandTestFence:
			{
				/* Every input message the EventDispatcher sent before the
				 * fence is already forwarded: tell koan_test_v1. */
				union koan_cmd_payload part = {};
				part.test_fence.serial
					= (uint32)message->GetInt32("koan:serial", 0);
				push_to_wayland(KOAN_CMD_TEST_FENCE_PART, part);
				break;
			}

			case B_MOUSE_WHEEL_CHANGED:
			{
				float dx = 0, dy = 0;
				message->FindFloat("be:wheel_delta_x", &dx);
				message->FindFloat("be:wheel_delta_y", &dy);
				uint32 time = _Time(message);
				for (int horizontal = 0; horizontal < 2; horizontal++) {
					float steps = horizontal ? dx : dy;
					if (steps == 0)
						continue;
					union koan_cmd_payload payload = {};
					payload.pointer_axis.toplevel = fToplevel;
					payload.pointer_axis.time = time;
					payload.pointer_axis.horizontal = horizontal;
					payload.pointer_axis.steps = steps;
					push_to_wayland(KOAN_CMD_POINTER_AXIS, payload);
				}
				break;
			}

			case B_KEY_DOWN:
			case B_UNMAPPED_KEY_DOWN:
			case B_KEY_UP:
			case B_UNMAPPED_KEY_UP:
			{
				int32 repeat = 0;
				if (message->FindInt32("be:key_repeat", &repeat) == B_OK
					&& repeat > 0)
					break;
				int32 key = 0;
				message->FindInt32("key", &key);
				uint32 evdev = haiku_to_evdev(key);
				if (evdev == 0)
					break;
				union koan_cmd_payload payload = {};
				payload.keyboard_key.toplevel = fToplevel;
				payload.keyboard_key.time = _Time(message);
				payload.keyboard_key.key = evdev;
				payload.keyboard_key.pressed = message->what == B_KEY_DOWN
					|| message->what == B_UNMAPPED_KEY_DOWN;
				push_to_wayland(KOAN_CMD_KEYBOARD_KEY, payload);
				break;
			}

			default:
				BLooper::MessageReceived(message);
				break;
		}
	}

private:
	static uint32 _Time(BMessage *message)
	{
		bigtime_t when = 0;
		if (message->FindInt64("when", &when) != B_OK)
			when = system_time();
		return (uint32)(when / 1000);
	}

	void _Mouse(BMessage *message)
	{
		BPoint where;
		if (message->FindPoint("screen_where", &where) != B_OK
			&& message->FindPoint("where", &where) != B_OK)
			return;
		int32 buttons = 0;
		message->FindInt32("buttons", &buttons);
		uint32 time = _Time(message);

		double x = where.x - atomic_get(&fLeft);
		double y = where.y - atomic_get(&fTop);
		bool inside = x >= 0 && y >= 0 && x < atomic_get(&fWidth)
			&& y < atomic_get(&fHeight);
		x += atomic_get(&fOffsetX);
		y += atomic_get(&fOffsetY);

		union koan_cmd_payload payload = {};
		payload.pointer_motion.toplevel = fToplevel;
		payload.pointer_motion.time = time;
		if (fPopup && !inside && fInside && fButtons == 0) {
			/* Leaving a popup, maybe onto its own toplevel, whose looper
			 * reports the new position too: a leave from here could
			 * arrive after that and clear the new focus. One last motion
			 * lets Koan's hit-test pick what is there instead. */
			payload.pointer_motion.x = x;
			payload.pointer_motion.y = y;
			push_to_wayland(KOAN_CMD_POINTER_MOTION, payload);
		} else if (inside || fButtons != 0) {
			/* A held button keeps the client informed outside the window
			 * too (drags); the Wayland side keeps its implicit grab. */
			payload.pointer_motion.x = x;
			payload.pointer_motion.y = y;
			push_to_wayland(KOAN_CMD_POINTER_MOTION, payload);
		} else if (fInside) {
			payload.pointer_motion.leave = 1;
			push_to_wayland(KOAN_CMD_POINTER_MOTION, payload);
		}
		fInside = inside;

		static const struct {
			int32 mask;
			uint32 button;
		} kButtons[] = {
			{ B_PRIMARY_MOUSE_BUTTON, 0x110 },   /* BTN_LEFT */
			{ B_SECONDARY_MOUSE_BUTTON, 0x111 }, /* BTN_RIGHT */
			{ B_TERTIARY_MOUSE_BUTTON, 0x112 },  /* BTN_MIDDLE */
			{ 0x08, 0x113 },                     /* BTN_SIDE */
			{ 0x10, 0x114 },                     /* BTN_EXTRA */
		};
		int32 changed = buttons ^ fButtons;
		for (size_t i = 0; i < sizeof(kButtons) / sizeof(kButtons[0]); i++) {
			if ((changed & kButtons[i].mask) == 0)
				continue;
			union koan_cmd_payload button = {};
			button.pointer_button.toplevel = fToplevel;
			button.pointer_button.time = time;
			button.pointer_button.button = kButtons[i].button;
			button.pointer_button.pressed = (buttons & kButtons[i].mask) != 0;
			push_to_wayland(KOAN_CMD_POINTER_BUTTON, button);
		}
		fButtons = buttons;
	}

	struct wlr_xdg_toplevel *fToplevel;
	int32 fLeft, fTop, fWidth, fHeight;     /* set from other threads */
	int32 fOffsetX, fOffsetY;               /* set from other threads */
	int32 fButtons;                         /* looper thread only */
	bool fInside;
	bool fPopup;
};

/* -------------------------------------------------------------------------- */
/* WaylandServerWindow                                                        */
/* -------------------------------------------------------------------------- */

/* The ServerWindow of a Wayland toplevel. There is no BWindow client to
 * notify, so the desktop's notifications become Wayland requests instead:
 *
 *  B_QUIT_REQUESTED (close button)  -> xdg_toplevel.close
 *  B_MINIMIZE                       -> native minimize/restore
 *  B_ZOOM (zoom button)             -> maximize/restore (configure)
 *  B_WINDOW_RESIZED (border drag)   -> configure with the new size
 *  B_WINDOW_ACTIVATED               -> configure with the activated state
 *
 * SendMessageToClient() runs on the Desktop or window thread, often with
 * window locks held, so it only queues commands; nothing here touches
 * wlroots objects or calls back into the Desktop. The toplevel and the
 * bridge handle are keys the consumers validate before use. */
class WaylandServerWindow : public ServerWindow {
public:
	/* Takes ownership of the running input looper; its port becomes this
	 * window's client looper port, so the EventDispatcher sends the
	 * window's input there. */
	WaylandServerWindow(const char *title, ServerApp *app,
			struct wlr_xdg_toplevel *toplevel, wayland_window *handle,
			team_id clientTeam, WaylandInputLooper *input, bool popup = false)
		:
		ServerWindow(title, app, -1, input->Port(), -1),
		fToplevel(toplevel),
		fHandle(handle),
		fInput(input),
		fClientWidth(0),
		fClientHeight(0),
		fPopup(popup)
	{
		/* All Wayland windows share one ServerApp (app_server's team);
		 * report the client process instead, so window lists per team
		 * (Deskbar) group them under the app the registrar knows. */
		if (clientTeam > 0)
			_SetClientTeam(clientTeam);
	}

	virtual ~WaylandServerWindow()
	{
		if (fInput->Lock())
			fInput->Quit();
	}

	/* The native frame, for input coordinates (later updates arrive as
	 * B_WINDOW_MOVED/RESIZED). */
	void SetFrame(BRect frame)
	{
		fInput->SetOrigin((int32)frame.left, (int32)frame.top);
		fInput->SetSize(frame.IntegerWidth() + 1, frame.IntegerHeight() + 1);
	}

	wayland_window *Handle() const { return fHandle; }
	bool IsPopup() const { return fPopup; }

	/* A popup's position in its toplevel's content (input offset). */
	void SetContentOffset(int32 x, int32 y)
	{
		fInput->SetContentOffset(x, y);
	}

	/* Size of the client's last frame, so a resize the bridge made to
	 * follow the client is not sent back to it as a new configure. */
	void SetClientSize(int32 width, int32 height)
	{
		atomic_set(&fClientWidth, width);
		atomic_set(&fClientHeight, height);
	}

	virtual status_t SendMessageToClient(const BMessage *message,
			int32 target = B_NULL_TOKEN) const
	{
		(void)target;
		if (fPopup) {
			/* A popup's window has no xdg_toplevel of its own: fToplevel
			 * is its parent's, which must not be closed, configured or
			 * (de)activated by what happens to the popup window. Only
			 * keep the input geometry current. */
			BPoint where;
			int32 width = 0, height = 0;
			if (message->what == B_WINDOW_MOVED
				&& message->FindPoint("where", &where) == B_OK)
				fInput->SetOrigin((int32)where.x, (int32)where.y);
			else if (message->what == B_WINDOW_RESIZED
				&& message->FindInt32("width", &width) == B_OK
				&& message->FindInt32("height", &height) == B_OK)
				fInput->SetSize(width + 1, height + 1);
			return B_OK;
		}
		switch (message->what) {
			case B_QUIT_REQUESTED:
			{
				union koan_cmd_payload payload = {};
				payload.toplevel_close.toplevel = fToplevel;
				_Push(KOAN_CMD_TOPLEVEL_CLOSE, payload);
				break;
			}

			case B_MINIMIZE:
			{
				bool minimize = true;
				message->FindBool("minimize", &minimize);
				if (gWaylandWindowBridge) {
					gWaylandWindowBridge->QueueNativeAction(fHandle,
						KOAN_NATIVE_MINIMIZE, minimize ? 1 : 0);
				}
				break;
			}

			case B_ZOOM:
				if (gWaylandWindowBridge) {
					gWaylandWindowBridge->QueueNativeAction(fHandle,
						KOAN_NATIVE_ZOOM, 0);
				}
				break;

			case B_WINDOW_MOVED:
			{
				BPoint where;
				if (message->FindPoint("where", &where) == B_OK)
					fInput->SetOrigin((int32)where.x, (int32)where.y);
				break;
			}

			case B_WINDOW_RESIZED:
			{
				int32 width = 0, height = 0;
				if (message->FindInt32("width", &width) != B_OK
					|| message->FindInt32("height", &height) != B_OK)
					break;
				width++;
				height++;
				fInput->SetSize(width, height);
				if (width == atomic_get(&fClientWidth)
					&& height == atomic_get(&fClientHeight))
					break;
				union koan_cmd_payload payload = {};
				payload.toplevel_configure.toplevel = fToplevel;
				payload.toplevel_configure.width = width;
				payload.toplevel_configure.height = height;
				payload.toplevel_configure.maximized = -1;
				payload.toplevel_configure.activated = -1;
				_Push(KOAN_CMD_TOPLEVEL_CONFIGURE, payload);
				break;
			}

			case B_WINDOW_ACTIVATED:
			{
				bool active = false;
				message->FindBool("active", &active);
				union koan_cmd_payload payload = {};
				payload.toplevel_configure.toplevel = fToplevel;
				payload.toplevel_configure.maximized = -1;
				payload.toplevel_configure.activated = active ? 1 : 0;
				_Push(KOAN_CMD_TOPLEVEL_CONFIGURE, payload);
				/* The active window gets the keyboard, as the Desktop's
				 * focus window gets B_KEY_DOWN. */
				union koan_cmd_payload focus = {};
				focus.keyboard_focus.toplevel = fToplevel;
				focus.keyboard_focus.focused = active ? 1 : 0;
				_Push(KOAN_CMD_KEYBOARD_FOCUS, focus);
				break;
			}

			default:
				/* Nothing else has a Wayland meaning yet. */
				break;
		}
		return B_OK;
	}

private:
	void _Push(enum koan_cmd_type type, const union koan_cmd_payload &payload)
		const
	{
		push_to_wayland(type, payload);
	}

	struct wlr_xdg_toplevel *fToplevel;
	wayland_window *fHandle;
	WaylandInputLooper *fInput;  /* owned; quit in the destructor */
	mutable int32 fClientWidth;
	mutable int32 fClientHeight;
	bool fPopup;
};

/* -------------------------------------------------------------------------- */
/* WaylandDesktopListener                                                     */
/* -------------------------------------------------------------------------- */

/* What the Desktop does to native windows that Wayland popups must follow:
 *
 *  WindowMoved (a toplevel)  -> its popup windows move along
 *  MouseDown outside every Wayland client surface (a window frame, a
 *      native window, the desktop) -> menus close (xdg_popup.grab: "the
 *      compositor ... dismisses the popup" on such a click). Clicks on a
 *      client's content reach Koan, whose popup grab handles them.
 *
 * Called from Desktop/EventDispatcher code with window locks held: only
 * queue commands, never take the bridge lock or call into the Desktop. */
class WaylandDesktopListener : public DesktopListener {
public:
	WaylandDesktopListener(WaylandWindowBridge *bridge) : fBridge(bridge) {}

	virtual int32 Identifier() { return 'wlbr'; }
	virtual void ListenerRegistered(Desktop *desktop) { (void)desktop; }
	virtual void ListenerUnregistered() {}
	virtual bool HandleMessage(Window *sender, BPrivate::LinkReceiver &link,
		BPrivate::LinkSender &reply)
	{
		(void)sender; (void)link; (void)reply;
		return false;
	}
	virtual void WindowAdded(Window *window) { (void)window; }
	virtual void WindowRemoved(Window *window) { (void)window; }
	virtual bool KeyPressed(uint32 what, int32 key, int32 modifiers)
	{
		(void)what; (void)key; (void)modifiers;
		return false;
	}
	virtual void MouseEvent(BMessage *message) { (void)message; }

	virtual void MouseDown(Window *window, BMessage *message,
		const BPoint &where)
	{
		(void)message;
		if (!fBridge->HasPopups() || window == NULL)
			return;
		WaylandServerWindow *sw
			= dynamic_cast<WaylandServerWindow *>(window->ServerWindow());
		if (sw && (sw->IsPopup() || window->Frame().Contains(where)))
			return;
		union koan_cmd_payload none = {};
		push_to_wayland(KOAN_CMD_DISMISS_POPUPS, none);
	}

	virtual void MouseUp(Window *window, BMessage *message,
		const BPoint &where)
	{
		(void)window; (void)message; (void)where;
	}
	virtual void MouseMoved(Window *window, BMessage *message,
		const BPoint &where)
	{
		(void)window; (void)message; (void)where;
	}

	virtual void WindowMoved(Window *window)
	{
		if (!fBridge->HasPopups() || window == NULL)
			return;
		WaylandServerWindow *sw
			= dynamic_cast<WaylandServerWindow *>(window->ServerWindow());
		if (sw && !sw->IsPopup())
			fBridge->QueueNativeAction(sw->Handle(),
				KOAN_NATIVE_FOLLOW_POPUPS, 0);
	}

	virtual void WindowResized(Window *window) { (void)window; }
	virtual void WindowActivated(Window *window) { (void)window; }
	virtual void WindowSentBehind(Window *window, Window *behindOf)
	{
		(void)window; (void)behindOf;
	}
	virtual void WindowWorkspacesChanged(Window *window, uint32 workspaces)
	{
		(void)window; (void)workspaces;
	}
	virtual void WindowHidden(Window *window, bool fromMinimize)
	{
		(void)window; (void)fromMinimize;
	}
	virtual void WindowMinimized(Window *window, bool minimize)
	{
		(void)window; (void)minimize;
	}
	virtual void WindowTabLocationChanged(Window *window, float location,
		bool isShifting)
	{
		(void)window; (void)location; (void)isShifting;
	}
	virtual void SizeLimitsChanged(Window *window, int32 minWidth,
		int32 maxWidth, int32 minHeight, int32 maxHeight)
	{
		(void)window; (void)minWidth; (void)maxWidth; (void)minHeight;
		(void)maxHeight;
	}
	virtual void WindowLookChanged(Window *window, window_look look)
	{
		(void)window; (void)look;
	}
	virtual void WindowFeelChanged(Window *window, window_feel feel)
	{
		(void)window; (void)feel;
	}
	virtual bool SetDecoratorSettings(Window *window,
		const BMessage &settings)
	{
		(void)window; (void)settings;
		return false;
	}
	virtual void GetDecoratorSettings(Window *window, BMessage &settings)
	{
		(void)window; (void)settings;
	}

private:
	WaylandWindowBridge *fBridge;
};

void
WaylandWindowBridge::SetDesktop(Desktop *desktop)
{
	fDesktop = desktop;
	if (!desktop || fListener)
		return;
	fListener = new(std::nothrow) WaylandDesktopListener(this);
	/* Notifications walk the listener list under the window lock. */
	if (fListener && desktop->LockAllWindows()) {
		desktop->RegisterListener(fListener);
		desktop->UnlockAllWindows();
	}

	ScreenChanged(desktop->VirtualScreen().ScreenAt(0));
}

void
WaylandWindowBridge::ScreenChanged(::Screen *screen)
{
	if (screen == NULL)
		return;

	uint16 width, height;
	uint32 colorSpace;
	float frequency;
	screen->GetMode(width, height, colorSpace, frequency);

	/* Frame() is the framebuffer app_server draws, after panel rotation:
	 * the pixels a fullscreen client covers. */
	BRect frame = screen->Frame();

	union koan_cmd_payload payload;
	memset(&payload, 0, sizeof(payload));
	/* Frame() is logical; the screen is drawn at the render scale. The
	 * output mode is in buffer pixels and wlroots divides it by the scale,
	 * so the output's logical size is app_server's screen again. */
	float scale = screen->HWInterface() != NULL
		? screen->HWInterface()->RenderScaleFactor() : 1.0f;
	payload.output_mode.width
		= (int32_t)roundf((frame.IntegerWidth() + 1) * scale);
	payload.output_mode.height
		= (int32_t)roundf((frame.IntegerHeight() + 1) * scale);
	payload.output_mode.refresh_mhz = frequency > 0.0f
		? (int32_t)roundf(frequency * 1000.0f) : 0;
	payload.output_mode.scale = scale;
	push_to_wayland(KOAN_CMD_OUTPUT_MODE, payload);
}

WaylandWindowBridge::WaylandWindowBridge()
	:
	fLock("WaylandWindowBridge lock"),
	fWakePending(0),
	fDispatchThread(-1),
	fRunning(false),
	fDisplay(NULL),
	fDesktop(NULL),
	fServerApp(NULL),
	fNextViewToken(1),
	fTestWhere(0, 0),
	fPopupCount(0),
	fListener(NULL),
	fTestButtons(0)
{
	wl_list_init(&fWindows);
	wl_list_init(&fClients);
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
		request_state: s_request_state,
		request_minimize: s_request_minimize,
		create_popup: s_create_popup,
		move_popup: s_move_popup,
		wake_app_server: s_wake_app_server,
	};
	koan_adapter_set_window_callbacks(&cbs, NULL, this);

	/* The wake target is app_server's own looper; it is installed with
	 * SetWakeTarget() from AppServer before the dispatch thread starts.
	 * The bridge never owns it. */
	return B_OK;
}

void
WaylandWindowBridge::Shutdown()
{
	/* Terminate the Wayland event loop first. The dispatch thread blocks in
	 * wl_display_run(), so it must be stopped with wl_display_terminate()
	 * rather than the app_server wake port. */
	StopDispatchThread();

	if (fListener && fDesktop && fDesktop->LockAllWindows()) {
		fDesktop->UnregisterListener(fListener);
		fDesktop->UnlockAllWindows();
		delete fListener;
		fListener = NULL;
	}

	if (fDisplay) {
		koan_server_destroy(fDisplay);
		fDisplay = NULL;
	}

	/* The wake target belongs to app_server; just forget it. */
	fWakeTarget = BMessenger();

	BAutolock locker(fLock);
	struct wayland_window *ww, *tmp;
	wl_list_for_each_safe(ww, tmp, &fWindows, link) {
		destroy_native_window(ww);
		delete ww;
	}
	wl_list_init(&fWindows);
	unregister_all_clients();

	/* The shared client context outlives individual windows. Quit it
	 * after every ServerWindow is asked to quit, and wait for its loop.
	 * ServerApp::Quit() deletes the object itself (on its own thread when
	 * running, synchronously otherwise), so never delete it here: doing so
	 * was a double delete that crashed app_server on shutdown whenever a
	 * Wayland window had been open. Read the thread before Quit(), after
	 * which the object may already be gone. */
	if (fServerApp) {
		ServerApp *app = fServerApp;
		fServerApp = NULL;
		thread_id thread = app->Thread();
		app->Quit();
		if (thread >= B_OK)
			wait_for_thread(thread, NULL);
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

	/* The queue exists from koan_server_create() on; a desktop set before
	 * that could not report its screen. */
	if (fDesktop != NULL)
		ScreenChanged(fDesktop->VirtualScreen().ScreenAt(0));

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
	/* Clear the in-flight flag before draining: a command queued while we
	 * drain posts a fresh wake-up instead of being left behind. */
	atomic_set(&fWakePending, 0);
	DrainCommands();
}

void
WaylandWindowBridge::_Wake()
{
	if (!fWakeTarget.IsValid())
		return;

	/* Coalesce: one pending wake-up is enough, WaylandWakeup() drains the
	 * whole queue. */
	if (atomic_get_and_set(&fWakePending, 1) != 0)
		return;

	if (fWakeTarget.SendMessage(kMsgWaylandWake) != B_OK) {
		atomic_set(&fWakePending, 0);
		wlr_log(WLR_ERROR, "Wayland bridge: failed to wake app_server");
	}
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
	payload.create_window.app_id = toplevel->app_id
		? strdup(toplevel->app_id) : NULL;
	pid_t pid = 0;
	if (toplevel->base && toplevel->base->client)
		wl_client_get_credentials(toplevel->base->client->client, &pid,
			NULL, NULL);
	payload.create_window.pid = pid;

	/* Push the command without blocking. app_server processes it on its
	 * thread and replies via KOAN_CMD_WINDOW_READY, which the Wayland
	 * thread drains through koan_adapter_drain_commands(). The queue
	 * deep-copies the title, so our copy is always ours to free. */
	struct koan_command *queued = koan_queue_push(queue,
		KOAN_CMD_CREATE_WINDOW, &payload, NULL);
	free(payload.create_window.title);
	free(payload.create_window.app_id);
	if (!queued) {
		wlr_log(WLR_ERROR, "Wayland bridge: failed to queue CREATE_WINDOW");
		return;
	}

	/* Notify app_server that a command is pending. */
	bridge->_Wake();
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

	bridge->_Wake();
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

	bridge->_Wake();
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

	bridge->_Wake();
}

void
WaylandWindowBridge::s_set_buffer(void *userdata, struct koan_queue *queue,
		void *window, void *data, int32_t width, int32_t height,
		int32_t stride, uint32_t format, float scale)
{
	WaylandWindowBridge *bridge = (WaylandWindowBridge *)userdata;
	if (!bridge || !queue || !window || !data) {
		free(data); /* set_buffer takes ownership */
		return;
	}

	/* 'data' is the adapter's composed copy, never wlroots memory: hand
	 * it to the queue as is. The queue frees it after
	 * process_set_buffer(). Copying it again cost a full-window memcpy
	 * (59 MB at 5K) per frame. */
	union koan_cmd_payload payload = {0};
	payload.set_buffer.window = window;
	payload.set_buffer.data = data;
	payload.set_buffer.width = width;
	payload.set_buffer.height = height;
	payload.set_buffer.stride = stride;
	payload.set_buffer.format = format;
	payload.set_buffer.scale = scale >= 1.0f ? scale : 1.0f;
	koan_queue_push(queue, KOAN_CMD_SET_BUFFER, &payload, NULL);

	bridge->_Wake();
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

	bridge->_Wake();
}

/* The client asked to (un)maximize or (un)set fullscreen. Decide on the
 * app_server thread, like the zoom button (process_native_action()). */
void
WaylandWindowBridge::s_request_state(void *userdata, struct koan_queue *queue,
		void *window, int32_t maximized, int32_t fullscreen)
{
	(void)queue;
	WaylandWindowBridge *bridge = (WaylandWindowBridge *)userdata;
	if (!bridge || !window)
		return;
	if (maximized >= 0) {
		bridge->QueueNativeAction(window, KOAN_NATIVE_SET_MAXIMIZED,
			maximized);
	}
	if (fullscreen >= 0) {
		bridge->QueueNativeAction(window, KOAN_NATIVE_SET_FULLSCREEN,
			fullscreen);
	}
}

/* koan_test_v1 queued a command outside the window callbacks. */
void
WaylandWindowBridge::s_wake_app_server(void *userdata)
{
	WaylandWindowBridge *bridge = (WaylandWindowBridge *)userdata;
	if (bridge)
		bridge->_Wake();
}

/* xdg_toplevel.set_minimized: minimize like the decorator's button
 * (process_native_action(), app_server thread). */
void
WaylandWindowBridge::s_request_minimize(void *userdata,
		struct koan_queue *queue, void *window)
{
	(void)queue;
	WaylandWindowBridge *bridge = (WaylandWindowBridge *)userdata;
	if (bridge && window)
		bridge->QueueNativeAction(window, KOAN_NATIVE_MINIMIZE, 1);
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
	case KOAN_CMD_NATIVE_ACTION:
		return bridge->process_native_action(cmd);
	case KOAN_CMD_CREATE_POPUP:
		return bridge->process_create_popup(cmd);
	case KOAN_CMD_MOVE_POPUP:
		return bridge->process_move_popup(cmd);
	case KOAN_CMD_TEST_PLACE:
		return bridge->process_test_place(cmd);
	case KOAN_CMD_TEST_POINTER:
		return bridge->process_test_pointer(cmd);
	case KOAN_CMD_TEST_FENCE:
		return bridge->process_test_fence(cmd);
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
		const char *title, int32_t width, int32_t height,
		const BRect *popupFrame, int32 offsetX, int32 offsetY)
{
	if (!fDesktop)
		return B_ERROR;

	status_t status = EnsureServerApp();
	if (status != B_OK)
		return status;

	/* The window's input goes to this looper (its "client"), which
	 * forwards it to the Wayland client. */
	WaylandInputLooper *input = new(std::nothrow) WaylandInputLooper(
		ww->toplevel, popupFrame != NULL);
	if (!input)
		return B_NO_MEMORY;
	input->SetContentOffset(offsetX, offsetY);
	if (input->Run() < B_OK) {
		delete input;
		return B_ERROR;
	}

	WaylandServerWindow *server_window = new(std::nothrow) WaylandServerWindow(
		title && title[0] ? title : "Wayland Window",
		fServerApp, ww->toplevel, ww, ww->pid, input, popupFrame != NULL);
	if (!server_window) {
		if (input->Lock())
			input->Quit();
		return B_NO_MEMORY;
	}
	server_window->SetClientSize(width, height);

	/* A normal titled window: the decorator draws the tab (title, close,
	 * zoom, minimize) and the window joins native tab stacking. Clients
	 * are asked to leave the frame to us (server-side decorations). Offset
	 * from the screen corner so the tab is on screen. */
	BRect frame(0, 0, width - 1, height - 1);
	frame.OffsetBy(40, 40);
	window_look look = B_TITLED_WINDOW_LOOK;
	window_feel feel = B_NORMAL_WINDOW_FEEL;
	uint32 flags = 0;
	if (popupFrame != NULL) {
		/* Like a native menu (BMenuWindow): no frame, above every
		 * window, never takes the focus, so the toplevel stays active
		 * and keeps its popup (deactivation dismisses popups). */
		frame = *popupFrame;
		look = B_NO_BORDER_WINDOW_LOOK;
		feel = kMenuWindowFeel;
		flags = B_NOT_MOVABLE | B_NOT_CLOSABLE | B_NOT_ZOOMABLE
			| B_NOT_MINIMIZABLE | B_NOT_RESIZABLE | B_AVOID_FOCUS;
	}
	server_window->SetFrame(frame);
	status = server_window->Init(frame, look, feel, flags,
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

	/* A BWindow client creates its root view with AS_VIEW_CREATE_ROOT;
	 * Wayland clients never send it, so create it here. Without a top
	 * view Window::SetHidden() dereferences NULL in ShowWindow() and
	 * SET_BUFFER has nowhere to put the client's pixels. View tokens live
	 * in the owning ServerApp's token space, which all Wayland windows
	 * share, so each top view needs its own token. */
	View *top_view = new(std::nothrow) View(
		IntRect(0, 0, width - 1, height - 1), IntPoint(0, 0),
		"wayland surface", fNextViewToken++, B_FOLLOW_ALL_SIDES,
		B_WILL_DRAW);
	if (!top_view || top_view->InitCheck() != B_OK) {
		delete top_view;
		server_window->Quit();
		return B_NO_MEMORY;
	}
	top_view->CurrentState()->SetFont(fServerApp->PlainFont());
	if (fDesktop->LockAllWindows()) {
		window->SetTopView(top_view);
		fDesktop->UnlockAllWindows();
	} else {
		delete top_view;
		server_window->Quit();
		return B_ERROR;
	}

	/* Run the window's message loop, as ServerApp::_CreateWindow() does
	 * for BWindow clients. Desktop repaints (show, expose, move, windows
	 * on top going away) only queue a dirty region and RequestRedraw();
	 * the loop is what calls Window::RedrawDirtyRegion() to paint the
	 * border and the top view background, i.e. the client's pixels.
	 * Without it the window area stayed black. Messages the loop tries
	 * to send back to the (non-existent) BWindow client fail harmlessly
	 * on the invalid client ports. */
	status = server_window->Run();
	if (status != B_OK) {
		server_window->Quit();
		return status;
	}

	ww->server_window = server_window;
	ww->window = window;
	ww->input = BMessenger(input);

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
	 * desktop in its destructor. Its message loop is running, so Quit()
	 * hides the window and asks the loop to delete it asynchronously;
	 * forget the pointers right away. */
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
	ww->maximized = false;
	ww->fullscreen = false;
	ww->fullscreen_restore_look = B_TITLED_WINDOW_LOOK;
	ww->is_popup = false;
	ww->popup = NULL;
	ww->popup_x = 0;
	ww->popup_y = 0;
	ww->pid = cmd->payload.create_window.pid;

	status_t status = create_native_window(ww, title, width, height);
	if (status != B_OK) {
		delete ww;
		cmd->payload.create_window.window_out = NULL;
		return -1;
	}

	wl_list_insert(&fWindows, &ww->link);
	client_window_added(ww->pid, cmd->payload.create_window.app_id);
	cmd->payload.create_window.window_out = ww;

	/* CREATE_WINDOW is only queued once the toplevel is mapped, so the
	 * native window starts visible. Re-maps are handled by the adapter's
	 * SET_VISIBILITY command. */
	if (fDesktop && ww->window && !ww->visible) {
		/* What AS_SHOW_OR_HIDE_WINDOW does for a BWindow's Show(): the
		 * show level starts at 1, and window_info reports it, so Deskbar
		 * would list a window left at 1 as hidden ("No windows"). */
		ww->window->SetShowLevel(0);
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
	if (!ww->is_popup)
		client_window_removed(ww->pid);
	else
		atomic_add(&fPopupCount, -1);
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
	float scale = cmd->payload.set_buffer.scale >= 1.0f
		? cmd->payload.set_buffer.scale : 1.0f;
	/* The window's content size in window (logical) coordinates; the
	 * bitmap has 'scale' pixels per unit and is drawn into that rect. At
	 * a matching screen scale, Painter copies it 1:1. The adapter rounds
	 * the pixel size, which rounding back undoes for scales of 1 and up. */
	int32_t logicalWidth = (int32_t)lroundf(width / scale);
	int32_t logicalHeight = (int32_t)lroundf(height / scale);
	if (logicalWidth <= 0 || logicalHeight <= 0)
		return 0;

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

	/* The window's message loop paints this view concurrently. Take the
	 * same locks it holds, in the same order: the ServerWindow's own lock,
	 * then the desktop lock. */
	if (!fDesktop || !ww->server_window->Lock()) {
		gBitmapManager->BitmapRemoved(bitmap);
		return -1;
	}

	/* Follow the client's size. ResizeWindowBy() takes the all-windows
	 * write lock itself, as for AS_WINDOW_RESIZE. Record the size first so
	 * the resulting B_WINDOW_RESIZED is not sent back as a configure. */
	static_cast<WaylandServerWindow *>(ww->server_window)->SetClientSize(
		logicalWidth, logicalHeight);
	BRect frame = native_window->Frame();
	int32 dw = logicalWidth - (frame.IntegerWidth() + 1);
	int32 dh = logicalHeight - (frame.IntegerHeight() + 1);
	if (dw != 0 || dh != 0)
		fDesktop->ResizeWindowBy(native_window, dw, dh);

	if (!fDesktop->LockSingleWindow()) {
		ww->server_window->Unlock();
		gBitmapManager->BitmapRemoved(bitmap);
		return -1;
	}

	/* The view takes its own reference. The bitmap is not client-visible,
	 * so drop the manager registration and our creation reference; the
	 * view owns it until it is replaced or destroyed. */
	/* Clients render at the integer output scale (2 at 125 to 200%), so at
	 * a fractional screen scale their buffer is shrunk to the screen's
	 * density. Nearest neighbour drops whole rows and columns of it and
	 * leaves strokes of uneven width; filter instead, as macOS and other
	 * compositors do for surfaces not drawn at the panel's density. At a
	 * matching density the copy stays 1:1 and unfiltered. */
	float renderScale = fDesktop->HWInterface() != NULL
		? fDesktop->HWInterface()->RenderScaleFactor() : 1.0f;
	uint32 bitmapOptions = fabsf(scale - renderScale) > 0.001f
		? B_FILTER_BITMAP_BILINEAR : 0;
	top_view->SetViewBitmap(bitmap, IntRect(0, 0, width - 1, height - 1),
		IntRect(0, 0, logicalWidth - 1, logicalHeight - 1),
		0 /* no tiling */, bitmapOptions);
	gBitmapManager->BitmapRemoved(bitmap);

	/* MarkContentDirty() intersects its arguments in place, so pass
	 * copies: handing it VisibleRegion() itself clipped the window's own
	 * visible region down to its content and lost the border. The
	 * expose part repaints the view background, i.e. the new bitmap,
	 * right away. */
	BRegion dirty(native_window->VisibleContentRegion());
	BRegion expose(dirty);
	native_window->MarkContentDirty(dirty, expose);

	fDesktop->UnlockSingleWindow();
	ww->server_window->Unlock();
	return 0;
}

/* -------------------------------------------------------------------------- */
/* Registrar / Deskbar                                                        */
/* -------------------------------------------------------------------------- */

/* "application/x-vnd.wayland.<app_id>", reduced to characters valid in a
 * MIME type. Falls back to the executable's name. Deskbar recognizes this
 * prefix; keep both in sync. */
static void
wayland_signature(const char *app_id, const char *exe, BString &signature)
{
	const char *name = app_id && app_id[0] ? app_id : exe;
	if (!name || !name[0])
		name = "client";
	signature = "application/x-vnd.wayland.";
	for (const char *c = name; *c && signature.Length() < B_MIME_TYPE_LENGTH
			- 1; c++) {
		/* Case is kept: the app_id names the client's desktop entry
		 * (<app_id>.desktop, case-sensitive), which Deskbar looks up for
		 * the name and icon. Signatures compare case-insensitively. */
		char ch = *c;
		signature += isalnum((unsigned char)ch) || ch == '.' || ch == '-'
			|| ch == '_' || ch == '+' ? ch : '-';
	}
}

/* Path of a client's executable. /proc/<pid>/exe first; sandboxed clients
 * (Firefox) make themselves non-dumpable, which hides it even from the same
 * user, so fall back to argv[0] from /proc/<pid>/cmdline (always readable),
 * resolved against PATH when it is not absolute. */
static bool
client_executable(int32 pid, char *path, size_t size)
{
	char procPath[64];
	snprintf(procPath, sizeof(procPath), "/proc/%d/exe", (int)pid);
	ssize_t length = readlink(procPath, path, size - 1);
	if (length > 0) {
		path[length] = '\0';
		return true;
	}

	snprintf(procPath, sizeof(procPath), "/proc/%d/cmdline", (int)pid);
	FILE *file = fopen(procPath, "r");
	if (!file)
		return false;
	char argv0[PATH_MAX];
	size_t read = fread(argv0, 1, sizeof(argv0) - 1, file);
	fclose(file);
	if (read == 0)
		return false;
	argv0[read] = '\0';     /* argv[0] ends at the first NUL */

	if (argv0[0] == '/') {
		strlcpy(path, argv0, size);
		return access(path, X_OK) == 0;
	}
	const char *env = getenv("PATH");
	BString dirs(env ? env : "/usr/bin:/bin");
	int32 start = 0;
	while (start <= dirs.Length()) {
		int32 end = dirs.FindFirst(':', start);
		if (end < 0)
			end = dirs.Length();
		BString dir;
		dirs.CopyInto(dir, start, end - start);
		snprintf(path, size, "%s/%s", dir.String(), argv0);
		if (dir.Length() > 0 && access(path, X_OK) == 0)
			return true;
		start = end + 1;
	}
	return false;
}

void
WaylandWindowBridge::client_window_added(int32 pid, const char *app_id)
{
	if (pid <= 0)
		return;

	wayland_client *client;
	wl_list_for_each(client, &fClients, link) {
		if (client->pid == pid) {
			client->windows++;
			return;
		}
	}

	client = new(std::nothrow) wayland_client;
	if (!client)
		return;
	client->pid = pid;
	client->windows = 1;
	client->registered = false;
	wl_list_insert(&fClients, &client->link);

	/* Register the process like a launched app, so Deskbar's team list
	 * (be_roster) shows it and its windows (team == pid, see
	 * WaylandServerWindow). The ref names the executable, which gives
	 * Deskbar its name. */
	char exePath[PATH_MAX];
	if (!client_executable(pid, exePath, sizeof(exePath))) {
		wlr_log(WLR_ERROR, "Wayland bridge: no executable for pid %d",
			(int)pid);
		return;
	}

	entry_ref ref;
	if (get_ref_for_path(exePath, &ref) != B_OK) {
		wlr_log(WLR_ERROR, "Wayland bridge: no entry_ref for %s", exePath);
		return;
	}

	BString signature;
	const char *leaf = strrchr(exePath, '/');
	wayland_signature(app_id, leaf ? leaf + 1 : exePath, signature);

	uint32 token = 0;
	team_id otherTeam = -1;
	status_t status = BRoster::Private().AddApplication(signature.String(),
		&ref, B_MULTIPLE_LAUNCH, pid, pid, -1, true, &token, &otherTeam);
	if (status != B_OK) {
		wlr_log(WLR_ERROR, "Wayland bridge: registering %s (pid %d) failed: "
			"%s", signature.String(), (int)pid, strerror(status));
		return;
	}
	client->registered = true;
}

void
WaylandWindowBridge::client_window_removed(int32 pid)
{
	wayland_client *client, *tmp;
	wl_list_for_each_safe(client, tmp, &fClients, link) {
		if (client->pid != pid)
			continue;
		if (--client->windows > 0)
			return;
		/* Last window gone: the app leaves Deskbar. */
		if (client->registered)
			BRoster::Private().RemoveApp(pid);
		wl_list_remove(&client->link);
		delete client;
		return;
	}
}

void
WaylandWindowBridge::unregister_all_clients()
{
	wayland_client *client, *tmp;
	wl_list_for_each_safe(client, tmp, &fClients, link) {
		if (client->registered)
			BRoster::Private().RemoveApp(client->pid);
		wl_list_remove(&client->link);
		delete client;
	}
	wl_list_init(&fClients);
}

void
WaylandWindowBridge::QueueNativeAction(void *window, int32 action, int32 arg)
{
	struct koan_queue *queue = koan_adapter_queue();
	if (!queue)
		return;

	union koan_cmd_payload payload = {};
	payload.native_action.window = window;
	payload.native_action.action = action;
	payload.native_action.arg = arg;
	if (koan_queue_push(queue, KOAN_CMD_NATIVE_ACTION, &payload, NULL))
		_Wake();
}

int
WaylandWindowBridge::process_native_action(struct koan_command *cmd)
{
	const struct koan_cmd_native_action *a = &cmd->payload.native_action;

	BAutolock locker(fLock);

	/* The window may have been destroyed since the action was queued. */
	wayland_window *ww = NULL;
	wayland_window *w;
	wl_list_for_each(w, &fWindows, link) {
		if (w == a->window) {
			ww = w;
			break;
		}
	}
	if (!ww || !ww->window || !fDesktop)
		return 0;

	switch (a->action) {
		case KOAN_NATIVE_MINIMIZE:
			/* What a BWindow client does when it gets B_MINIMIZE. */
			fDesktop->MinimizeWindow(ww->window, a->arg != 0);
			break;
		case KOAN_NATIVE_ZOOM:
			zoom_window(ww);
			break;
		case KOAN_NATIVE_SET_MAXIMIZED:
			/* xdg_toplevel.set_/unset_maximized: the zoom button's
			 * maximize/restore, but idempotent; a fullscreen window keeps
			 * its frame. xdg-shell needs a configure either way. */
			if ((a->arg != 0) != ww->maximized && !ww->fullscreen)
				zoom_window(ww);
			else
				configure_current_state(ww);
			break;
		case KOAN_NATIVE_FOLLOW_POPUPS:
			follow_popups(ww);
			break;
		case KOAN_NATIVE_SET_FULLSCREEN:
			if ((a->arg != 0) != ww->fullscreen)
				fullscreen_window(ww, a->arg != 0);
			else
				configure_current_state(ww);
			break;
		default:
			break;
	}
	return 0;
}

/* -------------------------------------------------------------------------- */
/* xdg_popup windows                                                          */
/* -------------------------------------------------------------------------- */

void
WaylandWindowBridge::s_create_popup(void *userdata, struct koan_queue *queue,
		struct wlr_xdg_popup *popup, struct wlr_xdg_toplevel *toplevel,
		int32_t x, int32_t y, int32_t width, int32_t height)
{
	WaylandWindowBridge *bridge = (WaylandWindowBridge *)userdata;
	if (!bridge || !queue)
		return;
	union koan_cmd_payload payload = {};
	payload.create_popup.popup = popup;
	payload.create_popup.toplevel = toplevel;
	payload.create_popup.x = x;
	payload.create_popup.y = y;
	payload.create_popup.width = width;
	payload.create_popup.height = height;
	if (koan_queue_push(queue, KOAN_CMD_CREATE_POPUP, &payload, NULL))
		bridge->_Wake();
}

void
WaylandWindowBridge::s_move_popup(void *userdata, struct koan_queue *queue,
		void *window, int32_t x, int32_t y)
{
	WaylandWindowBridge *bridge = (WaylandWindowBridge *)userdata;
	if (!bridge || !queue)
		return;
	union koan_cmd_payload payload = {};
	payload.move_popup.window = window;
	payload.move_popup.x = x;
	payload.move_popup.y = y;
	if (koan_queue_push(queue, KOAN_CMD_MOVE_POPUP, &payload, NULL))
		bridge->_Wake();
}

/* A menu, tooltip or popover: a borderless native window at its place
 * relative to the toplevel's content, above all windows, never focused.
 * Its input goes to the toplevel's key in the toplevel's content
 * coordinates; Koan's hit-test finds the popup surface there. */
int
WaylandWindowBridge::process_create_popup(struct koan_command *cmd)
{
	const struct koan_cmd_create_popup *p = &cmd->payload.create_popup;
	union koan_cmd_payload ready = {};
	ready.popup_ready.popup = p->popup;
	ready.popup_ready.window = NULL;

	BAutolock locker(fLock);
	wayland_window *parent = find_window_by_toplevel(p->toplevel);
	wayland_window *ww = NULL;
	if (parent && parent->window && parent->visible && fDesktop
		&& p->width > 0 && p->height > 0)
		ww = new(std::nothrow) wayland_window;
	if (ww) {
		ww->toplevel = p->toplevel;
		ww->server_window = NULL;
		ww->window = NULL;
		ww->visible = false;
		ww->maximized = false;
		ww->fullscreen = false;
		ww->fullscreen_restore_look = B_NO_BORDER_WINDOW_LOOK;
		ww->pid = parent->pid;
		ww->is_popup = true;
		ww->popup = p->popup;

		BPoint origin = parent->window->Frame().LeftTop();
		BRect frame(0, 0, p->width - 1, p->height - 1);
		frame.OffsetTo(origin.x + p->x, origin.y + p->y);
		if (create_native_window(ww, "Wayland popup", p->width, p->height,
				&frame, p->x, p->y) != B_OK) {
			delete ww;
			ww = NULL;
		}
	}
	if (ww) {
		ww->popup_x = p->x;
		ww->popup_y = p->y;
		atomic_add(&fPopupCount, 1);
		wl_list_insert(&fWindows, &ww->link);
		ww->window->SetShowLevel(0);
		fDesktop->ShowWindow(ww->window);
		ww->visible = true;
		ready.popup_ready.window = ww;
	} else {
		wlr_log(WLR_ERROR, "Wayland bridge: no window for a popup (parent "
			"%s)", parent ? "not shown" : "unknown");
	}
	koan_queue_push(koan_adapter_queue(), KOAN_CMD_POPUP_READY, &ready, NULL);
	return 0;
}

int
WaylandWindowBridge::process_move_popup(struct koan_command *cmd)
{
	const struct koan_cmd_move_popup *p = &cmd->payload.move_popup;
	BAutolock locker(fLock);
	wayland_window *ww = NULL, *w;
	wl_list_for_each(w, &fWindows, link) {
		if (w == p->window && w->is_popup) {
			ww = w;
			break;
		}
	}
	wayland_window *parent = ww ? find_window_by_toplevel(ww->toplevel) : NULL;
	if (!ww || !ww->window || !parent || !parent->window || !fDesktop)
		return 0;
	BPoint origin = parent->window->Frame().LeftTop();
	BRect frame = ww->window->Frame();
	fDesktop->MoveWindowBy(ww->window, origin.x + p->x - frame.left,
		origin.y + p->y - frame.top);
	ww->popup_x = p->x;
	ww->popup_y = p->y;
	static_cast<WaylandServerWindow *>(ww->server_window)
		->SetContentOffset(p->x, p->y);
	return 0;
}

/* The toplevel's native window moved (dragged, zoomed, fullscreen): keep
 * its popups at their place in its content. */
void
WaylandWindowBridge::follow_popups(wayland_window *parent)
{
	if (!parent->window || parent->is_popup || !fDesktop)
		return;
	BPoint origin = parent->window->Frame().LeftTop();
	struct wayland_window *w;
	wl_list_for_each(w, &fWindows, link) {
		if (!w->is_popup || w->toplevel != parent->toplevel || !w->window)
			continue;
		BRect frame = w->window->Frame();
		float dx = origin.x + w->popup_x - frame.left;
		float dy = origin.y + w->popup_y - frame.top;
		if (dx != 0 || dy != 0)
			fDesktop->MoveWindowBy(w->window, dx, dy);
	}
}

/* -------------------------------------------------------------------------- */
/* koan_test_v1 (server/koan_test.c; only with /run/koan-test-control)        */
/* -------------------------------------------------------------------------- */

/* Place a window the way the user would drag it: the native window moves,
 * the Desktop repaints and re-targets the pointer. */
int
WaylandWindowBridge::process_test_place(struct koan_command *cmd)
{
	const struct koan_cmd_test_place *p = &cmd->payload.test_place;
	BAutolock locker(fLock);
	wayland_window *ww = find_window_by_toplevel(p->toplevel);
	if (!ww || !ww->window || !fDesktop) {
		wlr_log(WLR_ERROR, "koan-test: place for a toplevel without a "
			"native window");
		return 0;
	}
	BRect frame = ww->window->Frame();
	fDesktop->MoveWindowBy(ww->window, p->x - frame.left, p->y - frame.top);
	return 0;
}

static int32
evdev_to_be_button(uint32 button)
{
	switch (button) {
		case 0x110: return B_PRIMARY_MOUSE_BUTTON;    /* BTN_LEFT */
		case 0x111: return B_SECONDARY_MOUSE_BUTTON;  /* BTN_RIGHT */
		case 0x112: return B_TERTIARY_MOUSE_BUTTON;   /* BTN_MIDDLE */
		case 0x113: return 0x08;                      /* BTN_SIDE */
		case 0x114: return 0x10;                      /* BTN_EXTRA */
		default: return 0;
	}
}

/* A mouse event as LibEvdevEventStream makes it, entering the
 * EventDispatcher like device input: hit-testing, click-to-focus and the
 * window's input looper all run as for a real mouse. */
int
WaylandWindowBridge::process_test_pointer(struct koan_command *cmd)
{
	const struct koan_cmd_test_pointer *p = &cmd->payload.test_pointer;
	if (!fDesktop)
		return 0;
	uint32 what = B_MOUSE_MOVED;
	if (p->button == 0) {
		fTestWhere = BPoint(p->x, p->y);
	} else {
		int32 mask = evdev_to_be_button(p->button);
		if (mask == 0)
			return 0;
		if (p->pressed)
			fTestButtons |= mask;
		else
			fTestButtons &= ~mask;
		what = p->pressed ? B_MOUSE_DOWN : B_MOUSE_UP;
	}
	BMessage *event = new(std::nothrow) BMessage(what);
	if (!event)
		return 0;
	event->AddPoint("where", fTestWhere);
	event->AddInt32("buttons", fTestButtons);
	event->AddInt32("modifiers", 0);
	event->AddInt64("when", system_time());
	if (what == B_MOUSE_DOWN)
		event->AddInt32("clicks", 1);
	fDesktop->EventDispatcher().InjectEvent(event);
	return 0;
}

/* Start a sync fence behind the injected input: the EventDispatcher hands
 * it back (TestFenceReached) once everything before it is dispatched. */
int
WaylandWindowBridge::process_test_fence(struct koan_command *cmd)
{
	uint32_t serial = cmd->payload.test_fence.serial;
	BMessage *fence = fDesktop && fWakeTarget.IsValid()
		? new(std::nothrow) BMessage(kEventStreamFence) : NULL;
	if (!fence) {
		union koan_cmd_payload begin = {};
		begin.test_fence.serial = serial;
		begin.test_fence.parts = 0;
		push_to_wayland(KOAN_CMD_TEST_FENCE_BEGIN, begin);
		return 0;
	}
	fence->AddMessenger("target", fWakeTarget);
	fence->AddInt32("koan:serial", (int32)serial);
	fDesktop->EventDispatcher().InjectEvent(fence);
	return 0;
}

/* Each input looper forwards its window's input in order; a fence message
 * behind it is answered once that input reached the Wayland queue. */
void
WaylandWindowBridge::TestFenceReached(BMessage *fence)
{
	int32 serial = fence->GetInt32("koan:serial", 0);
	int32 parts = 0;
	{
		BAutolock locker(fLock);
		struct wayland_window *ww;
		wl_list_for_each(ww, &fWindows, link) {
			BMessage part(kMsgWaylandTestFence);
			part.AddInt32("koan:serial", serial);
			if (ww->input.IsValid() && ww->input.SendMessage(&part) == B_OK)
				parts++;
		}
	}
	union koan_cmd_payload begin = {};
	begin.test_fence.serial = (uint32)serial;
	begin.test_fence.parts = parts;
	push_to_wayland(KOAN_CMD_TEST_FENCE_BEGIN, begin);
}

/* Answer a state request that changes nothing with the current state. */
void
WaylandWindowBridge::configure_current_state(wayland_window *ww)
{
	union koan_cmd_payload payload = {};
	payload.toplevel_configure.toplevel = ww->toplevel;
	payload.toplevel_configure.maximized = ww->maximized ? 1 : 0;
	payload.toplevel_configure.activated = -1;
	payload.toplevel_configure.fullscreen = ww->fullscreen
		? KOAN_STATE_ENTER : KOAN_STATE_LEAVE;
	koan_queue_push(koan_adapter_queue(), KOAN_CMD_TOPLEVEL_CONFIGURE,
		&payload, NULL);
}

/* Fullscreen: the native window drops its decorator and covers the screen;
 * leaving restores the look and the frame. As with zoom_window(), the window
 * moves now and its size follows the client's next frame at the configured
 * size (process_set_buffer()). */
void
WaylandWindowBridge::fullscreen_window(wayland_window *ww, bool enter)
{
	::Window *window = ww->window;
	BRect frame = window->Frame();
	BRect target;

	if (enter) {
		if (!fDesktop->LockSingleWindow())
			return;
		const ::Screen *screen = window->Screen();
		BRect screenFrame = screen ? screen->Frame()
			: BRect(0, 0, 1023, 767);
		ww->fullscreen_restore_look = window->Look();
		fDesktop->UnlockSingleWindow();

		ww->fullscreen_restore_frame = frame;
		ww->fullscreen = true;
		target = screenFrame;
		fDesktop->SetWindowLook(window, B_NO_BORDER_WINDOW_LOOK);
	} else {
		target = ww->fullscreen_restore_frame;
		ww->fullscreen = false;
		fDesktop->SetWindowLook(window, ww->fullscreen_restore_look);
	}

	fDesktop->MoveWindowBy(window, target.left - frame.left,
		target.top - frame.top);

	union koan_cmd_payload payload = {};
	payload.toplevel_configure.toplevel = ww->toplevel;
	payload.toplevel_configure.width = target.IntegerWidth() + 1;
	payload.toplevel_configure.height = target.IntegerHeight() + 1;
	payload.toplevel_configure.maximized = -1;
	payload.toplevel_configure.activated = -1;
	payload.toplevel_configure.fullscreen = enter
		? KOAN_STATE_ENTER : KOAN_STATE_LEAVE;
	koan_queue_push(koan_adapter_queue(), KOAN_CMD_TOPLEVEL_CONFIGURE,
		&payload, NULL);
}

/* Maximize/restore. The native window moves now; its size follows when the
 * client commits a frame at the configured size (process_set_buffer()). */
void
WaylandWindowBridge::zoom_window(wayland_window *ww)
{
	::Window *window = ww->window;
	BRect frame = window->Frame();
	BRect target;

	if (!ww->maximized) {
		if (!fDesktop->LockSingleWindow())
			return;
		const ::Screen *screen = window->Screen();
		BRect screenFrame = screen ? screen->Frame()
			: BRect(0, 0, 1023, 767);
		/* Keep the decorator (tab and borders) on screen. */
		BRect footprint = frame;
		if (window->Decorator())
			footprint = window->Decorator()->GetFootprint().Frame();
		fDesktop->UnlockSingleWindow();

		target = screenFrame;
		target.left += frame.left - footprint.left;
		target.top += frame.top - footprint.top;
		target.right -= footprint.right - frame.right;
		target.bottom -= footprint.bottom - frame.bottom;
		if (!target.IsValid())
			return;
		ww->restore_frame = frame;
		ww->maximized = true;
	} else {
		target = ww->restore_frame;
		ww->maximized = false;
	}

	fDesktop->MoveWindowBy(window, target.left - frame.left,
		target.top - frame.top);

	union koan_cmd_payload payload = {};
	payload.toplevel_configure.toplevel = ww->toplevel;
	payload.toplevel_configure.width = target.IntegerWidth() + 1;
	payload.toplevel_configure.height = target.IntegerHeight() + 1;
	payload.toplevel_configure.maximized = ww->maximized ? 1 : 0;
	payload.toplevel_configure.activated = -1;
	koan_queue_push(koan_adapter_queue(), KOAN_CMD_TOPLEVEL_CONFIGURE,
		&payload, NULL);
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

	/* Keep the show level in step, as AS_SHOW_OR_HIDE_WINDOW does. */
	if (visible && !ww->visible) {
		ww->window->SetShowLevel(0);
		fDesktop->ShowWindow(ww->window);
		ww->visible = true;
	} else if (!visible && ww->visible) {
		ww->window->SetShowLevel(1);
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
		/* Popup windows carry their toplevel's key for input only. */
		if (w->toplevel == toplevel && !w->is_popup)
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
