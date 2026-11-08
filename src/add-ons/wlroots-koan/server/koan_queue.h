/*
 * Koan thread-safe command queue for Wayland<->app_server communication.
 *
 * Two execution domains:
 *  - Wayland dispatch thread: owns wl_display, wlroots objects, surfaces, seats.
 *  - app_server thread: owns native Window/ServerWindow objects, bitmaps, input.
 *
 * All cross-thread operations pass through this queue. The queue itself is
 * thread-safe: either thread may push commands via koan_queue_push(). Each
 * command is addressed to exactly one consumer (app_server or the Wayland
 * dispatch thread); consumers drain only their own direction via
 * koan_queue_drain_direction().
 *
 * Memory ownership: koan_queue_push() deep-copies heap-owned payload fields
 * (e.g. strdup'd strings) so the queue owns its own copy. The sender must
 * free its original heap-allocated payload fields after push returns.
 * koan_queue_drain_direction() calls free_payload() after the processor
 * returns, so the processor must NOT free any payload memory it receives.
 *
 * Buffer ownership: any bitmap data handed to the queue (e.g. a copied SHM
 * buffer) is owned by the queue until the receiving thread processes the
 * command and frees it via free_payload(). Pointers to wlroots-owned memory
 * must NEVER be queued.
 */

#ifndef KOAN_QUEUE_H
#define KOAN_QUEUE_H

#include <pthread.h>
#include <semaphore.h>
#include <wayland-util.h>

#ifdef __cplusplus
extern "C" {
#endif

/* wlroots public headers are C-only and have no extern "C" guards. Include
 * them inside the block so C++ consumers get C linkage. */
#include <wlr/types/wlr_xdg_shell.h>

enum koan_cmd_type {
	/* Wayland thread -> app_server thread (native window lifecycle) */
	KOAN_CMD_CREATE_WINDOW,
	KOAN_CMD_DESTROY_WINDOW,
	KOAN_CMD_SET_TITLE,
	KOAN_CMD_SET_SIZE,
	KOAN_CMD_SET_BUFFER,
	KOAN_CMD_SET_VISIBILITY,

	/* app_server thread -> Wayland thread (input forwarding) */
	KOAN_CMD_POINTER_MOTION,
	KOAN_CMD_POINTER_BUTTON,
	KOAN_CMD_POINTER_AXIS,
	KOAN_CMD_KEYBOARD_KEY,
	KOAN_CMD_KEYBOARD_MODIFIERS,
	KOAN_CMD_KEYBOARD_FOCUS,
	/* app_server -> Wayland: native window creation completed */
	KOAN_CMD_WINDOW_READY,
	/* app_server -> Wayland: the user acted on the native window */
	KOAN_CMD_TOPLEVEL_CLOSE,
	KOAN_CMD_TOPLEVEL_CONFIGURE,

	/* app_server -> app_server: native window actions that must not run
	 * inside the Desktop call that requested them (it holds window
	 * locks); the bridge performs them when it drains the queue. */
	KOAN_CMD_NATIVE_ACTION,
};

/* Which thread is the intended consumer of a command. koan_queue_push()
 * derives this from the command type so a consumer can drain only its own
 * commands. Without this, two consumers sharing one list would race and
 * steal each other's commands. */
enum koan_queue_direction {
	KOAN_QUEUE_TO_APP_SERVER,
	KOAN_QUEUE_TO_WAYLAND,
};

/* KOAN_CMD_CREATE_WINDOW */
struct koan_cmd_create_window {
	struct wlr_xdg_toplevel *toplevel;
	int32_t width;
	int32_t height;
	char *title;        /* strdup'd by sender; queue owns its own copy */
	char *app_id;       /* xdg app_id or NULL; queue owns its own copy */
	int32_t pid;        /* client process (team for Deskbar); 0 unknown */
	void *window_out;   /* filled in by app_server; NULL on failure */
	bool done;          /* set true by app_server when processed */
};

/* KOAN_CMD_DESTROY_WINDOW */
struct koan_cmd_destroy_window {
	void *window;
};

/* KOAN_CMD_SET_TITLE */
struct koan_cmd_set_title {
	void *window;
	char *title;        /* strdup'd by sender, freed by receiver */
};

/* KOAN_CMD_SET_SIZE */
struct koan_cmd_set_size {
	void *window;
	int32_t width;
	int32_t height;
};

/* KOAN_CMD_SET_BUFFER */
struct koan_cmd_set_buffer {
	void *window;
	void *data;         /* Koan-owned memory; freed by receiver after copy */
	int32_t width;
	int32_t height;
	int32_t stride;
	uint32_t format;    /* DRM fourcc format (e.g. DRM_FORMAT_ARGB8888) */
};

/* KOAN_CMD_SET_VISIBILITY */
struct koan_cmd_set_visibility {
	void *window;
	bool visible;
};

/* KOAN_CMD_POINTER_MOTION */
struct koan_cmd_pointer_motion {
	uint32_t time;
	wl_fixed_t sx;
	wl_fixed_t sy;
};

/* KOAN_CMD_POINTER_BUTTON */
struct koan_cmd_pointer_button {
	uint32_t time;
	uint32_t button;
	uint32_t state;     /* WL_POINTER_BUTTON_STATE_PRESSED / RELEASED */
};

/* KOAN_CMD_POINTER_AXIS */
struct koan_cmd_pointer_axis {
	uint32_t time;
	uint32_t axis;
	wl_fixed_t value;
};

/* KOAN_CMD_KEYBOARD_KEY */
struct koan_cmd_keyboard_key {
	uint32_t time;
	uint32_t key;
	uint32_t state;     /* WL_KEYBOARD_KEY_STATE_PRESSED / RELEASED */
};

/* KOAN_CMD_KEYBOARD_MODIFIERS */
struct koan_cmd_keyboard_modifiers {
	uint32_t depressed;
	uint32_t latched;
	uint32_t locked;
	uint32_t group;
};

/* KOAN_CMD_KEYBOARD_FOCUS */
struct koan_cmd_keyboard_focus {
	struct wlr_xdg_toplevel *toplevel;
};

/* KOAN_CMD_WINDOW_READY */
struct koan_cmd_window_ready {
	struct wlr_xdg_toplevel *toplevel;
	void *window;
};

/* KOAN_CMD_TOPLEVEL_CLOSE: decorator close button -> xdg_toplevel.close.
 * The toplevel is only a key; the Wayland thread checks it is still alive
 * before using it. */
struct koan_cmd_toplevel_close {
	struct wlr_xdg_toplevel *toplevel;
};

/* KOAN_CMD_TOPLEVEL_CONFIGURE: native size/state -> xdg_toplevel.configure.
 * width/height 0 keep the client's size; maximized/activated -1 keep the
 * current state. */
struct koan_cmd_toplevel_configure {
	struct wlr_xdg_toplevel *toplevel;
	int32_t width;
	int32_t height;
	int32_t maximized;
	int32_t activated;
};

enum koan_native_action {
	KOAN_NATIVE_MINIMIZE,     /* arg: 1 minimize, 0 restore */
	KOAN_NATIVE_ZOOM,         /* maximize / restore */
};

/* KOAN_CMD_NATIVE_ACTION. window is the bridge's handle; the bridge
 * checks it is still alive before using it. */
struct koan_cmd_native_action {
	void *window;
	int32_t action;           /* enum koan_native_action */
	int32_t arg;
};

union koan_cmd_payload {
	struct koan_cmd_create_window create_window;
	struct koan_cmd_destroy_window destroy_window;
	struct koan_cmd_set_title set_title;
	struct koan_cmd_set_size set_size;
	struct koan_cmd_set_buffer set_buffer;
	struct koan_cmd_set_visibility set_visibility;
	struct koan_cmd_pointer_motion pointer_motion;
	struct koan_cmd_pointer_button pointer_button;
	struct koan_cmd_pointer_axis pointer_axis;
	struct koan_cmd_keyboard_key keyboard_key;
	struct koan_cmd_keyboard_modifiers keyboard_modifiers;
	struct koan_cmd_keyboard_focus keyboard_focus;
	struct koan_cmd_window_ready window_ready;
	struct koan_cmd_toplevel_close toplevel_close;
	struct koan_cmd_toplevel_configure toplevel_configure;
	struct koan_cmd_native_action native_action;
};

struct koan_command {
	enum koan_cmd_type type;
	enum koan_queue_direction direction; /* set by koan_queue_push() */
	union koan_cmd_payload payload;
	struct wl_list link;
	sem_t *completion_sem; /* for CREATE_WINDOW: Wayland thread waits for
				app_server to fill window_out */
};

/*
 * Create a command queue. Returns NULL on failure.
 * The queue owns a pipe used to wake the Wayland event loop.
 */
struct koan_queue *koan_queue_create(void);

/* Destroy the queue and all its resources. Any unprocessed commands are
 * dropped (their payload memory is freed if it is heap-allocated). */
void koan_queue_destroy(struct koan_queue *queue);

/*
 * Push a command onto the queue. Thread-safe: may be called from either
 * thread. The command's consumer direction is derived from its type.
 * The caller retains no ownership of payload memory after this returns;
 * the queue takes ownership and will free it when processed.
 * The queue's wake-up pipe notifies the Wayland event loop. Producers of
 * KOAN_QUEUE_TO_APP_SERVER commands must also wake app_server themselves:
 * the bridge writes its wake port after each push.
 * For KOAN_CMD_CREATE_WINDOW, pass a completion_sem that the receiver will
 * post once window_out is filled; the sender can wait on it.
 * Returns the queued command pointer on success, or NULL on failure.
 */
struct koan_command *koan_queue_push(struct koan_queue *queue,
				     enum koan_cmd_type type,
				     const union koan_cmd_payload *payload,
				     sem_t *completion_sem);

/*
 * Get the wake-up file descriptor. Register this with wl_event_loop_add_fd()
 * on the Wayland event loop so the thread is woken when app_server pushes
 * a command. Returns -1 if the queue has no wake-up pipe.
 */
int koan_queue_get_wake_fd(struct koan_queue *queue);

/*
 * Drain and process pending commands addressed to `direction`.
 * Must be called by the consumer thread for its own direction only:
 *  - app_server thread: KOAN_QUEUE_TO_APP_SERVER
 *  - Wayland dispatch thread: KOAN_QUEUE_TO_WAYLAND
 * Commands addressed to the other direction are left queued. The processor
 * callback is invoked without holding the queue lock and must not free the
 * command or its payload. Returns the number of commands processed.
 */
int koan_queue_drain_direction(struct koan_queue *queue,
			       enum koan_queue_direction direction,
			       int (*processor)(struct koan_command *cmd,
						void *userdata),
			       void *userdata);

/* Wake the Wayland thread (write to the wake-up pipe). Safe to call from
 * app_server after pushing a command. Returns 0 on success. */
int koan_queue_wake(struct koan_queue *queue);

/* Signal shutdown: wake the thread so it can exit cleanly. */
void koan_queue_shutdown(struct koan_queue *queue);

#ifdef __cplusplus
}
#endif

#endif /* KOAN_QUEUE_H */
