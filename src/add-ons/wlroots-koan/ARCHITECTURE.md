# wlroots-koan Architecture Comparison

## Legacy Injected Server (vendor/wayland-server-master / HaiWay)

| Aspect | Legacy Implementation |
|--------|----------------------|
| Transport | `wl_client_create_ips()` — Haiku IPC, not a Unix socket |
| Client model | **Single client only** (`fClient` stores one `wl_client *`) |
| Server lifecycle | Created on first client connection (`wl_ips_client_connected`) |
| Event loop | Separate `BLooper` ("wayland", `B_DISPLAY_PRIORITY`) |
| Integration | Injected into application architecture via `wl_ips_client_connected` |
| Display | `wl_display_create()` called lazily on first client |
| Compositor | `HaikuCompositorGlobal` — renders directly into Haiku windows |
| Multi-client | Not supported — `fClient` is overwritten on each connection |
| Disconnect | `wl_ips_client_disconnected()` — no cleanup of other clients |
| Socket | None — uses Haiku message ports |

### Limitations of the Legacy Model

1. **Single-client**: Only one Wayland client can connect at a time. The `Application::fClient` member holds a single pointer, overwritten on each new connection.
2. **No Unix domain socket**: Uses Haiku's IPC mechanism (`wl_client_create_ips`), which is not a standard Wayland socket. External Wayland applications cannot connect via `WLAYLAND_SOCKET` or `wayland-0`.
3. **Injected architecture**: The server is injected into the client application's process space, violating the separation between server and clients.
4. **No clean shutdown**: `wl_display_destroy_clients()` and `wl_display_destroy()` are commented out — resources leak on disconnect.
5. **Event loop separation**: A separate `BLooper` runs the Wayland event loop, disconnected from app_server's main event loop.

---

## New Socket-Based Server (wlroots-koan)

| Aspect | New Implementation |
|--------|-------------------|
| Transport | **Unix domain socket** via `wl_display_add_socket_auto()` |
| Client model | **Multi-client** — each `wl_client` is independent |
| Server lifecycle | Explicit `koan_server_create()` / `koan_server_destroy()` |
| Event loop | Integrated with app_server's event loop via `wl_display_get_event_loop()` |
| Integration | Runs as an `app_server` subsystem, owning the Wayland endpoint |
| Display | `wl_display_create()` at server init, socket added immediately |
| Compositor | app_server owns compositing; wlroots manages Wayland surfaces |
| Multi-client | Fully supported — each client gets its own `wl_client`, resources, surfaces |
| Disconnect | `wl_client_destroy()` + resource cleanup; other clients unaffected |
| Socket | Real Unix domain socket (e.g., `/run/user/1000/wayland-0`) |

### Key Design Principles

1. **Socket-based IPC**: Standard Wayland Unix domain socket, compatible with external Wayland clients.
2. **Multi-client**: Each connection creates an independent `wl_client` with its own protocol resources, surfaces, and window lifecycle.
3. **app_server authority**: app_server remains the authority over native windows, decorations, stacking, and compositing. wlroots handles Wayland protocol and surface management.
4. **In-process server**: The Wayland server runs in-process as an app_server subsystem, exposed via a real Unix domain socket.
5. **No second compositor**: wlroots does not implement a competing compositor. It manages Wayland surfaces; app_server composites them into native windows.

---

## Dependency and Ownership Diagram

```
Wayland Clients (external apps)
      |
      |  Unix Domain Socket (e.g., wayland-0)
      v
+-------------------------------+
|  libwayland-server (wl_display)|
|  - wl_display_create()        |
|  - wl_display_add_socket()    |
|  - wl_display_get_event_loop()|
+-------------------------------+
      |
      |  Wayland protocol
      v
+-------------------------------+
|  wlroots (Wayland protocol)   |
|  - wl_compositor, wl_subcompositor |
|  - xdg_shell (xdg_toplevel)   |
|  - wlr_seat, wlr_pointer, ... |
|  - wlr_buffer, wlr_shm        |
+-------------------------------+
      |
      |  Surface lifecycle events
      v
+-------------------------------+
|  Koan Wayland Adapter         |
|  - koan_server.c (socket)     |
|  - koan_adapter.c (surfaces)  |
|  - backend/koan (wlroots      |
|    backend for Koan display)  |
+-------------------------------+
      |
      |  Native window operations
      v
+-------------------------------+
|  app_server                   |
|  - Window creation/destruction|
|  - Decorations, stacking      |
|  - Focus, activation          |
|  - Compositing, display       |
+-------------------------------+
      |
      |  Native graphics/input
      v
+-------------------------------+
|  AccelerantHWInterface        |
|  - DRM/GBM/KMS                |
|  - Display output             |
+-------------------------------+
      |
      v
   Display
```

---

## Multi-Client Lifecycle Design

### Client Connection

1. Client calls `wl_display_connect(socket_name)` → connects to Unix domain socket
2. `wl_display_add_socket_auto()` accepts the connection
3. libwayland-server creates a `struct wl_client` for the connection
4. wlroots binds Wayland globals (compositor, xdg_shell, seat, etc.) to the client
5. Client creates `wl_surface` objects → wlroots tracks them
6. Client creates `xdg_toplevel` → Koan adapter creates a native Koan window

### Concurrent Clients

- Client A and Client B connect simultaneously to the same socket
- Each gets its own `wl_client *`, protocol resources, and surface tree
- Each `xdg_toplevel` creates an independent native Koan window
- Buffers are owned per-client; Client A's buffers are not accessible to Client B
- Input focus is managed per-seat; only one client has keyboard focus at a time

### Client Disconnection

1. Client closes connection or calls `wl_display_disconnect()`
2. libwayland-server destroys the `wl_client` and all its resources
3. Koan adapter receives surface destroy events → destroys native Koan windows
4. Other clients continue unaffected
5. Server keeps running; new clients can connect

### Server Shutdown

1. `wl_display_destroy_clients()` — destroy all clients
2. `wl_display_destroy()` — destroy the display and close the socket
3. All native Koan windows created by Wayland clients are destroyed

---

## wlroots-koan Server Initialization and Shutdown

### Initialization (`koan_server_create`)

```c
1. wl_display_create()           — create Wayland display
2. wl_display_add_socket_auto()  — create Unix domain socket
3. wlroots globals registration  — compositor, xdg_shell, seat, shm, etc.
4. Koan backend creation         — wlr_koan_backend_create()
5. Koan adapter init             — koan_adapter_init(xdg_shell)
6. Event loop integration        — wl_display_get_event_loop()
7. Socket name published         — via wayland-0 or WAYLAND_DISPLAY
```

### Shutdown (`koan_server_destroy`)

```c
1. wl_display_destroy_clients()  — disconnect all clients
2. wl_display_destroy()          — close socket, destroy display
3. Koan backend destroy          — cleanup Koan-specific state
4. Adapter cleanup               — destroy remaining native windows
```

---

## Event-Loop Integration with app_server

The Wayland server's event loop (`wl_display_get_event_loop()`) must be integrated with app_server's main event loop. Two approaches:

### Option A: Poll-based integration (recommended)

app_server's main loop polls on both its own file descriptors and the Wayland display's file descriptor. When Wayland events are pending, `wl_display_dispatch_pending()` is called.

### Option B: Dedicated Wayland thread

A separate thread runs `wl_display_run()` (or `wl_display_dispatch()` in a loop), communicating with app_server via messages.

For the initial implementation, Option A is preferred as it avoids threading complexity.

---

## Unix Socket Creation and Client Connection Design

### Socket Creation

```c
struct wl_display *display = wl_display_create();
const char *socket = wl_display_add_socket_auto(display);
// socket is "wayland-0", "wayland-1", etc.
// Published via WAYLAND_DISPLAY environment variable
```

### Client Connection Handling

libwayland-server handles socket acceptance internally. When a client connects:

1. `wl_client_create()` is called internally by libwayland-server
2. The client is added to the display's client list
3. Wayland globals are automatically bound to the new client
4. The client's events are dispatched via the event loop

### Multiple Clients

Each client connection creates a new `wl_client *`. All protocol objects (surfaces, toplevels, seats) are per-client. No shared state between clients except global Wayland objects.

---

## Native Window Adapter Design

### xdg_toplevel → Koan Window Mapping

1. Client creates `xdg_surface` → wlroots creates `wlr_xdg_surface`
2. Client creates `xdg_toplevel` → wlroots creates `wlr_xdg_toplevel`
3. Adapter receives `new_toplevel` event
4. Adapter creates a native Koan `Window` via app_server APIs
5. Adapter subscribes to `xdg_toplevel` events:
   - `set_title` → update window title
   - `set_size` → resize window
   - `request_maximize` → maximize/restore
   - `request_minimize` → minimize
   - `request_activate` → focus window
   - `request_fullscreen` → fullscreen
   - `close` → request close
   - `destroy` → destroy native window
6. Client commits buffer → adapter submits buffer to Koan window
7. app_server composites the window into the display

### Window Lifecycle

| Wayland Event | Koan Native Operation |
|---------------|----------------------|
| xdg_toplevel created | `Window` created via app_server |
| xdg_toplevel mapped | Window shown |
| xdg_toplevel unmapped | Window hidden |
| xdg_toplevel destroyed | Window destroyed |
| Buffer committed | Window content updated |
| Request close | Window close requested |
| Request maximize | Window maximized/restored |
| Request minimize | Window minimized |

---

## CMake Integration Plan

### Build Option

- `VOS_WITH_WLROOTS` (default: OFF) — build wlroots-koan library
- When ON, `src/add-ons/wlroots-koan/` is added as a subdirectory

### Build Targets

- `wlroots` — static library (core wlroots + Koan platform layer)
- `wlroots-koan::wlroots` — alias for consumers

### Source Files

- Core wlroots sources (preserved verbatim from upstream)
- Koan platform layer (`backend/koan/`, `server/`)
- Compatibility headers (`backend/koan/drm_fourcc.h`, `xf86drm.h`, `xf86drmMode.h`)

### Dependencies

- wayland-server (≥1.24)
- wayland-client
- pixman-1
- xkbcommon
- libm, librt

### Excluded (not ported to Koan)

- DRM backend, libinput backend, X11 backend
- GLES2 renderer, Vulkan renderer
- GBM allocator, DRM allocator, udmabuf allocator
- Session support, Xwayland
- DRM syncobj (replaced by Koan eventfd implementation)

---

## Multi-Client Test Plan

### Test 1: Single Client Connection

1. Start wlroots-koan server
2. Connect one Wayland client
3. Client creates an `xdg_toplevel`
4. Verify native Koan window is created
5. Client commits a buffer
6. Verify window content is displayed
7. Client disconnects
8. Verify window is destroyed and server continues

### Test 2: Concurrent Clients

1. Start wlroots-koan server
2. Connect Client A → creates xdg_toplevel "App A"
3. Connect Client B → creates xdg_toplevel "App B"
4. Both clients commit buffers simultaneously
5. Verify both windows appear as separate native Koan windows
6. Client A disconnects
7. Verify Client B's window is unaffected
8. Client B disconnects
9. Verify server continues running

### Test 3: Client Reconnection

1. Client A connects, creates window, disconnects
2. Client A reconnects, creates new window
3. Verify new window is created correctly

### Test 4: Buffer Lifecycle

1. Client creates buffer, attaches to surface, commits
2. Server displays buffer content
3. Client replaces buffer with new content
4. Verify window updates
5. Client destroys buffer
5. Verify no use-after-free

### Test 5: Window Management

1. Client creates multiple xdg_toplevels
2. Client requests maximize → window maximized
3. Client requests minimize → window minimized
4. Client requests activate → window focused
5. Client sets title → window title updated

### Test Execution

Tests run as standalone Wayland client applications connecting to the wlroots-koan socket, verifying native Koan window creation and buffer presentation.
