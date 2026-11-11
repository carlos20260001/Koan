# Koan Wayland compositor: conformance-testing audit

Date: 2026-11-09. Scope: `src/add-ons/wlroots-koan/` (server, adapter, queue, tests,
build), the app_server bridge (`src/servers/app/WaylandWindow.*`), the repository
build/test/CI setup, WLCS (canonical/wlcs) and Weston's test suite as references.

This is the Phase 1 deliverable of the compliance-testing plan: no production
code was changed. The wlroots-koan sources carry uncommitted work from another
session; line numbers refer to the working tree on this date.

Evidence tags:

- **[V]** verified by reading the cited code, or by running the cited command
  in this session and observing its output.
- **[S]** assumption or inference that has not been run or fully traced.

---

## 0. Summary

- Koan is **not a separate compositor**. wlroots-koan is a static wlroots 0.20.2
  library plus a thin Koan layer (`server/koan_server.c`, `server/koan_adapter.c`,
  `server/koan_queue.c`) that owns the Wayland protocol objects. app_server owns
  windows, stacking, focus, input hit-testing and presentation. Protocol tests can
  run on the host against the real Koan server. Window placement, global pointer
  hit-testing and on-screen rendering exist only inside app_server [V].
- A real libwayland-client test already exists (`tests/koan_xdg_client_test.c`)
  and passes, together with two smaller tests (3/3, stable over 3 runs) [V].
- **Defects found by the audit:**
  1. `wl_shm` is advertised **twice** (confirmed by a registry dump) [V].
     **Fixed** in Milestone 1 (§10a).
  2. `xdg_popup` is not handled: the new-popup listener is empty, so popups
     never get a configure [V for the code, S for client symptoms].
  3. `wl_output` advertises a fixed 1920×1080, scale 1, unrelated to the real
     screen [V].
  4. No touch capability exists [V].
- WLCS needs gtest/gmock and C++20. The builder image has neither gtest nor
  `wayland-info` [V]. WLCS also needs absolute window positioning and absolute
  pointer coordinates. Koan has neither outside app_server, so the WLCS adapter
  must supply a small placement/hit-test stand-in. That stand-in is the main
  design decision (§6).
- Smallest useful first milestone: a native smoke + registry + negative-protocol
  test program in `wlroots-koan/tests/`, which fixes or pins the duplicate
  `wl_shm` (§10).

---

## 1. Current Wayland server architecture [V]

| Piece | Files / API | Notes |
|---|---|---|
| Library | `src/add-ons/wlroots-koan/CMakeLists.txt` | Static `wlroots` from upstream 0.20.2 sources + `backend/koan/`. DRM/libinput/X11 backends, GLES2/Vulkan, GBM, session and Xwayland are excluded. Built only when `VOS_WITH_WLROOTS=ON` (root `AGENTS.md`). |
| Server lifecycle | `server/koan_server.h`: `koan_server_create()`, `koan_server_add_socket()`, `koan_server_start()`, `koan_server_run()`, `koan_server_dispatch()`, `koan_server_destroy()` | **Singleton**: `static struct koan_server g_server` (`koan_server.c:97`). Destroy zeroes it (`:999`), so create→destroy→create is possible in principle, but only one server per process. |
| Socket | `koan_server_add_socket()` `koan_server.c:871-897` | Binds `$WAYLAND_DISPLAY`, falls back to `wl_display_add_socket_auto`. No fd-based client creation helper (WLCS needs one; `wl_client_create(display, fd)` is enough). |
| Globals created | `koan_server.c:741-839` | See §4. |
| Input from app_server | `koan_server_pointer_*`, `koan_server_keyboard_*` (`koan_server.h:58-71`, `koan_server.c:435-566`) | **Window-relative** coordinates per toplevel. No global pointer, no seat-level hit-testing. |
| Command queue | `server/koan_queue.{h,c}` | Two-direction queue, wake fd on the Wayland loop (`koan_server.c:854-860`). Commands: create/destroy window, title, size, buffer, visibility, pointer/keyboard, `WINDOW_READY`, `TOPLEVEL_CLOSE`, `TOPLEVEL_CONFIGURE`, `NATIVE_ACTION` (`koan_queue.h:43-66`). |
| Window adapter | `server/koan_adapter.{h,c}`; `struct koan_window_callbacks` (`koan_adapter.h:52-93`) | Maps xdg_toplevel lifecycle to native-window commands. Composes the whole surface tree (subsurfaces included) into one Koan-owned buffer (`compose_surface` `koan_adapter.c:149`, `deliver_buffer` `:509`). |
| app_server bridge | `src/servers/app/WaylandWindow.cpp` (`WaylandWindowBridge`) | Implements the callbacks, runs `koan_server_run()` on a dispatch thread, drains the queue on the app_server thread. |

## 2. Current compositor architecture [V]

- **Rendering:** the wlr renderer is pixman (`wlr_renderer_autocreate`,
  `koan_server.c:710`). It is used only so outputs, cursors and shm can
  initialise. Koan never renders an output frame: `koan_output_commit` just
  accepts the state (`koan_server.c:588-620`). On-screen pixels are produced by
  app_server's `Painter` from the buffer the adapter composes.
- **Scene graph:** `wlr_scene` and an output layout are created
  (`koan_server.c:778-790`) but windows are not placed in the scene for
  presentation. Window positions live in app_server [V for creation; S that the
  scene is unused for output: no `wlr_scene_output_commit` call found].
- **Frame callbacks:** fired right after each mapped commit, for the whole tree
  (`koan_any_surface_handle_commit` `koan_server.c:236-275`). They are not tied to
  when app_server actually presents.
- **Subsurfaces:** the commit handler skips surfaces whose
  `wlr_subsurface.synchronized` is set (`koan_server.c:243-247`). In wlroots 0.20
  that field is the **requested** mode; the *effective* mode is the static
  `subsurface_is_synchronized()` in `types/wlr_subcompositor.c:13`. For a
  desynchronized child of a synchronized subsurface, the two disagree [V]. Whether
  this delivers a partly applied tree is [S]: it needs a nested test (§9).
- **Popups:** `koan_server_handle_new_xdg_popup` is empty (`koan_server.c:341-345`).
- **Output:** one synthetic `wl_output`, custom mode 1920×1080@60, scale 1.0
  (`koan_server.c:595-645`). `koan_server_set_output_dimensions()` is a stub with
  no callers (`:648-662`).

## 3. Existing test infrastructure [V]

| Item | Where | State |
|---|---|---|
| Koan host tests | `src/add-ons/wlroots-koan/tests/` (`CMakeLists.txt`, 3 programs) | Built only with `-DWLROOTS_KOAN_BUILD_TESTS=ON` through a standalone wrapper project (`src/add-ons/wlroots-koan/AGENTS.md`, Layer 0). Plain ctest, no gtest. |
| `koan_server_create_test` | `tests/koan_server_create_test.c` | create/socket/start order. |
| `koan_queue_test` | `tests/koan_queue_test.c` | Queue ordering. |
| `koan_xdg_client_test` | `tests/koan_xdg_client_test.c` (865 lines) | Real libwayland client: registry, seat caps, keymap, initial configure, decoration mode, sync subsurface applied with the parent, desync subsurface commit, frame callbacks, pointer enter/motion/button/axis on the right (sub)surface, keyboard focus/keys, stale-toplevel input ignored, pointer leave, `TOPLEVEL_CONFIGURE` (maximize) and stale `TOPLEVEL_CLOSE`. Window callbacks stand in for app_server. |
| VOS tests | `src/tests/vos/CMakeLists.txt` | Executables only in Debug trees, no `add_test`. Nothing Wayland-related. |
| Sanitizers | `build/defs.cmake:33-48` (`configure --sanitize=thread|address`) | Opt-in for the whole tree. The wlroots-koan wrapper build has no sanitizer option. |
| CI | `.github/workflows/ccpp.yml`, `native-build.yml` | Build/bake only, Release, no `VOS_WITH_WLROOTS`, **no tests run**. |
| Runtime debugging | `docs/agents/runtime-debugging.md`, `docker/debug/*` | Headless QEMU, serial shell, QMP screenshots, deb update loop. This is the only way to exercise the app_server half. |

Baseline run (this session, builder image `localhost/vitruvian_build:latest`,
wrapper project in a scratch directory with `-DWLROOTS_KOAN_BUILD_TESTS=ON`):
`ctest --test-dir /tmp/wb/wlroots-koan/tests`, 3 times: **3/3 PASS each run** [V].
Note: ctest must point at the `tests/` subdirectory, because `enable_testing()`
is called inside the subproject (`CMakeLists.txt:486-490`); from the wrapper root
it reports "No tests were found".

## 4. Protocol coverage

Globals observed with a scratch registry-dump client against the real server
(same build, this session) [V]:

```text
org_kde_kwin_server_decoration_manager v1
wl_compositor v5
wl_data_device_manager v3
wl_output v4
wl_seat v9
wl_shm v2
wl_shm v2            <- duplicate
wl_subcompositor v1
xdg_wm_base v3
zxdg_decoration_manager_v1 v1
```

Cause of the duplicate: `wlr_renderer_init_wl_display()` already creates
`wl_shm` (`render/wlr_renderer.c:76-84`), and `koan_server_create()` creates a
second one with `wlr_shm_create_with_renderer(..., 2, ...)` (`koan_server.c:763`) [V].

Classification required by the plan:

| Protocol | Advertised | Implemented | Functional state | Tested today |
|---|---|---|---|---|
| `wl_display` / `wl_registry` | yes | libwayland | functional | registry roundtrip only |
| `wl_compositor` v5 | yes | wlroots | functional | indirectly |
| `wl_surface` | yes | wlroots + Koan commit hook | frame callbacks not paced by presentation | frame done checked |
| `wl_subcompositor` / `wl_subsurface` | yes | wlroots + Koan composition | sync/desync basic; nested effective-sync [S] | one sync + one desync child |
| `xdg_wm_base` v3 (upstream is at v7) | yes | wlroots + adapter | toplevel functional; **popup not functional** | toplevel only |
| `zxdg_decoration_manager_v1`, KDE server-decoration | yes | wlroots + Koan | functional | mode checked |
| `wl_shm` v2 | **twice** | wlroots | formats from pixman | buffer path only |
| `wl_seat` v9 pointer | yes | window-relative injection | functional inside one window | yes |
| `wl_seat` keyboard | yes (if keymap ready) | yes | functional | yes |
| `wl_touch` | no capability | wlroots code compiled, no Koan path | **not implemented** | no |
| `wl_output` v4 | yes | synthetic | **fixed 1920×1080 scale 1** | no |
| `wl_data_device_manager` v3 | yes | wlroots | clipboard/DnD not bridged to the BeAPI clipboard [S] | no |
| `wp_viewporter` | no | compiled (`CMakeLists.txt:87`) | not implemented | no |
| `wp_presentation` | no | compiled (`:85`) | not implemented | no |
| `wp_fractional_scale_v1` | no | compiled (`:102`) | not implemented | no |
| `zwp_linux_dmabuf_v1` | no (pixman: no DRM fd) | – | not applicable | – |
| `zwp_input_timestamps_v1`, text-input | no | – | not implemented | – |

## 5. Rendering backend coverage [V]

- Built: **pixman only** (root `AGENTS.md`; `wlr_renderer_autocreate`). GL/Vulkan
  renderers and the DRM backend are excluded from the build.
- Koan's own output path is app_server's `DrawingEngine`/`Painter` on DRM
  (`src/servers/app/drawing/interface/linux/drm/`). That path is exercised only in
  QEMU, never by host tests.
- So "renderer validation" has two levels: (a) wlroots-side buffer handling
  (shm import, release, damage) on pixman, testable on the host; (b) pixels on
  screen, testable only in QEMU via QMP screenshots (the method used for the
  fractional-scaling fixes on 2026-11-09).

## 6. Missing infrastructure for WLCS

WLCS facts below come from github.com/canonical/wlcs (README.rst,
`include/wlcs/display_server.h`, `pointer.h`, `touch.h`, `CMakeLists.txt`,
`src/main.cpp`) fetched on 2026-11-09. The latest release is **v1.8.1**; `main`
declares 1.8.2. `include/wlcs/keyboard.h` was not read.

- Entry point: `extern WlcsServerIntegration const wlcs_server_integration;`
  with `create_server(int argc, char const** argv)` / `destroy_server`.
- `WlcsDisplayServer` (version 4):
  - `start` must not block (the main loop on its own thread); `stop` blocks
    until it is torn down. A `start_on_this_thread` alternative exists (v3);
  - `create_client_socket` returns an fd that WLCS owns;
  - `position_window_absolute(server, wl_display* client, wl_surface*, x, y)`;
  - `create_pointer` (`move_absolute`/`move_relative` in `wl_fixed_t`,
    `button_up/down` with evdev codes), `create_touch`, `create_keyboard` (v4);
  - `get_descriptor` lists the supported extensions; WLCS skips tests for the
    missing ones.
- Run: `wlcs koan-wlcs-integration.so [gtest options]`. Sanitizer variants
  `wlcs.asan/.tsan/.ubsan` are built by default.
- Dependencies: CMake ≥ 3.5, **C++20**, GoogleTest/GMock (`find_package(GtestGmock)`),
  wayland-client/server/scanner (no minimum version declared), optional Boost.

Gaps in Koan:

| Need | Koan today | Required work |
|---|---|---|
| gtest/gmock, C++20 toolchain | builder image has GCC 14 (C++20 OK), **no gtest** [V] | Optional: separate test image layer or an explicit build-dependency step. Must not become a mandatory OS build dependency. |
| In-process start/stop on a thread | `koan_server_create/start/run` + `wl_display_terminate` already work from a thread (the xdg test does this) [V] | Thin wrapper. Must handle the singleton: WLCS creates and destroys a server per test. That create→destroy→create cycle is untested [S]. |
| Client from fd | no helper | `wl_client_create(display, fd)` on the server thread, after a `socketpair`. |
| Absolute window positions | owned by app_server; the host harness has none | **Adapter-side placement map** (toplevel → x,y) that implements `koan_window_callbacks` in place of app_server. This is the same role the xdg test's callbacks play. It stands in for app_server's window placement and does not add a second Wayland server. |
| Absolute pointer / hit-testing | window-relative only | Adapter converts absolute coordinates by hit-testing its placement map, then pushes `KOAN_CMD_POINTER_MOTION/BUTTON` (the same queue path app_server uses). |
| Keyboard | `KOAN_CMD_KEYBOARD_KEY/FOCUS` | Direct mapping. |
| Touch | none | `create_touch` → not implemented; descriptor and report must say so. |
| Extension descriptor | – | List exactly the advertised globals from §4. |

Consequence: WLCS validates the **Koan protocol layer** (wlroots-koan) with a test
stand-in for app_server. It does **not** validate app_server's own placement,
stacking or hit-testing. Those need the QEMU layer (§8). The report must state
this, as the plan's Section 1 warns.

## 7. Weston tests reproducible with standard clients

Source: `vendor/weston/` (Weston 16.0.90, commit `c2772abe70ea`, 2026-11-09;
read only, not built). `vendor/` is listed in `.gitignore`, so the tree is a
local reference and not part of the repository. The test files cited below are
under `vendor/weston/tests/`. Weston's current harness
declares tests with `DECLARE_TEST_LIST(TESTFN(...))`, runs each test program
with a fixture that launches Weston, and offers `weston_test_harness_execute_as_client`.
Plugin tests use `TESTFN_PLUGIN` and need `weston_compositor`. They are not
reusable.

| Weston file | Test cases | Weston private protocol needed? | Protocol errors expected | Koan applicability |
|---|---|---|---|---|
| `bad-buffer-test.c` | `test_truncated_shm_file` | no (harness only) | `wl_shm` `INVALID_FD` | **Direct port** with a plain client. |
| `viewporter-test.c` | 13 (`double_create`, `bad_source_rect`, `bad_destination_size`, `non_integer_destination_size`, `source_outside_buffer`, `no_surface_*`, …) | no | `VIEWPORT_EXISTS`, `BAD_SIZE`, `BAD_VALUE`, `NO_SURFACE`, `OUT_OF_BUFFER` | Ready once `wp_viewporter` is created; until then **NOT IMPLEMENTED**. |
| `roles-test.c` | `test_role_conflict_sub_wlshell`, `..._wlshell_sub` | no | `wl_subcompositor` `BAD_SURFACE`, `xdg_wm_base` `ROLE` | Rewrite with `xdg_surface` (Koan has no `wl_shell`). |
| `subsurface-test.c` | 23 (basic/position/placement protocol, paradox, identical/change link, nesting, loop paradox, `place_above/below` vs parent/grandparent/great-aunt/child/stranger/foreign, destroy protocol/permutations) | no | `wl_subcompositor` `BAD_SURFACE`, `wl_subsurface` `BAD_SURFACE` | **Direct port**, top priority (plan §7). |
| `presentation-test.c` | `test_presentation_feedback_simple` | no | – | **NOT IMPLEMENTED** until `wp_presentation`. |
| `text-test.c` | `text_test` | yes (`activate_surface`) | – | Not applicable (no text-input). |
| `touch-test.c` | 4 | yes (`send_touch`) | `weston_test` errors | Not implemented (no touch). |
| `pointer-test.c` | 14 (edges/corners, surface move, motion/button/axis events, timestamps) | yes (`move_pointer`, `send_button`, `send_axis`) | – | Port the event checks with `KOAN_CMD_POINTER_*` injection; the timestamps tests need `zwp_input_timestamps_v1` (not implemented). |
| `keyboard-test.c` | 4 | yes (`send_key`, `activate_surface`) | – | Port with `KOAN_CMD_KEYBOARD_*`; timestamps tests not implemented. |
| `input-timestamps-test.c` | – | – | – | The file does not exist on current `main`; the coverage lives in the pointer/keyboard/touch tests plus `input-timestamps-helper.c`. |

Ports must reimplement these cases with Koan's own helpers. They must not link
against or include Weston's harness (`weston-test-client-helper`,
`weston_test_*`). A file that reproduces Weston test cases is a derived work:
it keeps the copyright lines of the Weston files it derives from and the full
MIT permission notice, and it marks each derived case with its Weston source
(see `tests/koan_protocol_test.c`).

Where Weston and the protocol XML disagree, the XML wins. The Weston
expectations found so far that do **not** apply to Koan:

- `subsurface-test.c` expects `wl_subcompositor.bad_surface` when a surface
  is its own parent or would create a loop. Current `wayland.xml` requires
  `bad_parent`, and wlroots sends `bad_parent` [V].
- `bad-buffer-test.c` expects `wl_shm.invalid_fd` on a truncated pool. wlroots
  catches the SIGBUS and zero-fills the mapping instead (`types/wlr_shm.c:182-210`).
  For Koan the requirement is that the server and other clients survive [V].

## 8. What each layer can and cannot prove

| Layer | Proves | Cannot prove |
|---|---|---|
| Native host tests (libwayland client ↔ real Koan server, callbacks as app_server) | Protocol objects, errors, event order, subsurface state, frame callbacks, buffer copy/release, server survival after a client error | app_server placement, stacking, Painter output, real input |
| WLCS (same process model + placement stand-in) | Third-party conformance cases for the advertised protocols | Same as above |
| QEMU (`docker/debug/`) | Real Wayland clients on the real desktop, pixels, real input routing | Fine-grained protocol assertions |

## 9. Regressions that need dedicated tests

| # | Behaviour | Evidence | Test |
|---|---|---|---|
| R1 | One `wl_shm` global | duplicate observed [V] | registry: each interface advertised once, with the expected version |
| R2 | Startup must not abort (NULL `commit` impl, output without a renderer: fixed in `60b67ab5`) | comments `koan_server.c:600-640` [V] | covered by `koan_server_create_test`; extend to create→destroy→create (WLCS needs it) |
| R3 | Initial buffer-less commit is answered with a configure | `koan_server.c:255-262` [V] | covered (xdg test step 1) |
| R4 | Frame callbacks complete, or GTK/Firefox stall | `:266-274` [V] | covered for one tree; add a 100-frame loop and a nested tree |
| R5 | Effective vs requested subsurface sync | `:243-247` vs `wlr_subcompositor.c:13` [V/S] | desync child of a sync subsurface; parent destroyed with pending child state |
| R6 | `xdg_popup` gets a configure | empty handler [V] | popup create → expect configure. **FAIL expected today** |
| R7 | Input for a stale toplevel is ignored, and the server survives | covered (xdg test) | keep |
| R8 | Client protocol error disconnects only that client | not tested | two clients; one sends a bad request; the other must still roundtrip |
| R9 | `wl_output` mode/scale matches the screen | stub [V] | needs O12 of `fractional-scaling.md`; until then mark NOT IMPLEMENTED |

## 10. Proposed implementation sequence

1. **Milestone 1: smoke + registry + negative core.** One new test program
   `tests/koan_protocol_test.c` (plain C, ctest, same wrapper as Layer 0):
   - server start / socket / connect / roundtrip / disconnect / shutdown, plus
     create→destroy→create;
   - registry: each global exactly once, with versions (R1 goes red today);
   - negative cases: truncated shm fd (`WL_SHM_ERROR_INVALID_FD`), role
     conflict (subsurface vs xdg_surface), subsurface `BAD_SURFACE`. A second
     client must stay connected (R8);
   - a tiny result printer with PASS/FAIL/SKIPPED/NOT_IMPLEMENTED/BLOCKED
     lines, so skipped and unimplemented cases are counted separately;
   - fix R1 (drop the second `wlr_shm_create_with_renderer`) in the same
     milestone, red first.
   This needs no new dependencies and reuses the existing harness. It should be
   coordinated with the session that owns the uncommitted wlroots-koan changes,
   so the two don't edit the same files.
2. **Milestone 2: subsurface port** of Weston's 23 cases, plus R5 and frame
   callback loops.
3. **Milestone 3: xdg-shell**, popup lifecycle (R6 fix), configure/ack serial
   errors.
4. **Milestone 4: WLCS adapter** `koan-wlcs-integration` (C++, optional CMake
   option, its own container layer with gtest), with the placement/hit-test
   stand-in and an exact extension descriptor. The report lists every WLCS test
   as PASS/FAIL/SKIPPED (extension missing)/NOT IMPLEMENTED.
5. **Milestone 5: viewporter / presentation / fractional-scale**, together
   with fractional-scaling O12; then port the Weston viewporter and
   presentation tests.
6. **Milestone 6: QEMU end-to-end** smoke (a Wayland client maps, takes input,
   closes) scripted with `docker/debug/`. A CI job that runs the host tests in
   the builder image (and the existing build-only CI) is added only after the
   project agrees on CI changes.

The proposed CMake target names in the plan (`koan-wayland-tests`,
`koan-wlcs-integration`, …) do not exist yet. Milestone 1 keeps the existing
`tests/` directory and naming (`koan_*_test`) instead of creating a parallel
`tests/wayland/` tree.

## 10a. Milestone 1 status (2026-11-09)

Done. `tests/koan_protocol_test.c`, registered as three ctest entries (each
group in its own process):

| ctest | Cases | Result |
|---|---|---|
| `koan_protocol_core` | server start/shutdown; registry roundtrip, core globals, **each global once** (R1); bind with a too-high version or unknown name; shm zero-size pool, buffer outside the pool, unknown format, pool shrink; role conflicts (subsurface→xdg_surface, toplevel→subsurface); subsurface own parent, identical link, change link, loop, place_above stranger, place_below foreign, place_above child; server alive after errors; witness client connected throughout (R8). The inventory reports viewporter, presentation, fractional-scale, input-timestamps, text-input and touch as NOT_IMPLEMENTED | 22 PASS, 6 NOT_IMPLEMENTED |
| `koan_protocol_lifecycle` | create→serve→destroy twice in one process (WLCS model, R2) | 2 PASS |
| `koan_protocol_bad-buffer` | Weston bad-buffer port on a mapped toplevel | 1 PASS (no error posted; server and witness fine) |

- **R1 fixed:** `koan_server.c` no longer creates a second `wl_shm`.
  `wlr_renderer_init_wl_display()` provides the only one.
  - Before the fix, `registry-globals-unique` was red (`wl_shm x2`).
  - After it, the host ctest passes 6/6 on three consecutive runs.
  - In QEMU, `wayland-info` in the live session listed `wl_shm` twice with
    the ISO's app_server and once with the new debs.
- Output format: `RESULT|<status>|<case>|<detail>` plus a `SUMMARY` line.
  Exit status 1 on any FAIL or BLOCKED. SKIPPED and NOT_IMPLEMENTED are
  never counted as passes.
## 10b. Milestones 2–3 status (2026-11-09)

New ctest groups in `koan_protocol_test` (9 ctest entries in total, all
green 3 runs in a row in the builder image; release build and QEMU checked):

| ctest | Cases | Result |
|---|---|---|
| `koan_protocol_core` | + 7 more Weston subsurface negatives (place_above/below grandparent, great-aunt, child, stranger, foreign) and `surface-destroyed-before-role-object` (`wl_surface.defunct_role_object`) | 30 PASS, 6 NOT_IMPLEMENTED |
| `koan_protocol_subsurface` | Weston valid sequences: basic, position, placement, nesting, nesting_parent, place_above/below nested parent, destroy protocol, destroy permutations (7980 three-step orders of an 11-surface tree) | 9 PASS |
| `koan_protocol_compose` | 60-frame callback loop; nested tree composed; **effectively synchronized grandchild** waits for the root and is applied with it (R5); desync chain applies immediately; position applied on the parent commit; parent destroyed with pending child state | 8 PASS |
| `koan_protocol_xdg` | popup initial configure, positioner geometry, map, popup frame callback, destroy (R6) | 6 PASS, 1 NOT_IMPLEMENTED (`popup-displayed`) |

Fixes, each red first:

- **R5, nested sync**, in the vendored wlroots:
  - `types/wlr_subcompositor.c` `subsurface_handle_parent_commit()` released
    a child's cache only when the child's *requested* mode was sync. A
    desync-requested grandchild under a sync child was cached and then never
    applied, so its content stayed stuck.
  - It now tests `subsurface_is_synchronized()`. The patch is a marked local
    change; the code is identical to stock wlroots 0.20.2, so the issue
    should be reported upstream.
  - Red: `nested-tree-composed`, `nested-sync-grandchild-waits` and
    `nested-sync-applied-with-root` failed.
- **R6, popups**, in `koan_server.c`:
  - The initial popup commit now gets `wlr_xdg_surface_schedule_configure()`.
  - Mapped popups complete their frame callbacks.
  - Red: `popup-initial-configure` and `popup-positioner-geometry` failed.
  - Popups are still **not displayed**: `koan_window_callbacks` has no popup
    window. That needs app_server bridge work.

Spec deviations in Weston found while porting (Koan follows `wayland.xml`):

- Weston's `subsurface_destroy_protocol` and `destroy_permutations` destroy
  surfaces before their `wl_subsurface`. That is now `defunct_role_object`,
  so the ports destroy the role object first and the reverse is a negative
  case.

Runtime (QEMU, Debian's `weston` clients in the live session on the new
debs): `weston-simple-shm` and `weston-subsurfaces` map and keep animating.
The sub-surface demo shows its nested subsurfaces composed in the native
window.

Still open: popup display (bridge), CI.

## 10c. Milestone 4: WLCS (2026-11-10)

**Setup** (all optional; the OS build never uses it):

- `vendor/wlcs` is WLCS v1.8.1 (2b385328, 14 Aug 2025). It is a local
  reference in the gitignored `vendor/` directory.
- `docker/wlcs/Containerfile` builds the `localhost/koan-wlcs-test` image:
  the OS builder image plus googletest/gmock/boost from Debian 13.
- `docker/wlcs/run-wlcs.sh` builds WLCS (no sanitizer variants) and the
  integration module, runs the whole suite, and writes `generated.wlcs/`
  (`wlcs.xml`, `wlcs.log`, incremental build trees).
- `docker/wlcs/classify.py` maps every test to
  PASS/FAIL/SKIPPED/NOT_IMPLEMENTED/BLOCKED with a reason (`--json` for
  machine-readable output).
- `src/add-ons/wlroots-koan/tests/wlcs/koan_wlcs_integration.c` is built as
  `koan-wlcs-integration.so`, behind `-DWLROOTS_KOAN_BUILD_WLCS=ON
  -DWLCS_INCLUDE_DIR=…` (default OFF).
  - It runs the real Koan server through `start_on_this_thread`, so every
    WLCS call happens on the Wayland thread.
  - It stands in for app_server's window placement, stacking,
    click-to-focus and pointer hit-testing. It uses the same
    `koan_server_pointer_*` calls and queue commands as the bridge.
  - It is GPL-3.0-only, like WLCS (`debian/copyright`: GPL-3; the
    interface headers say "version 3"), with the license text in
    `tests/wlcs/COPYING.GPL3`. The same applies to
    `koan_wlcs_vm_integration.c` (§10e). The MIT wlroots-koan code they
    link is compatible with that. Neither is installed or packaged.

**Why WLCS runs on the host and not in the VM.** WLCS loads the
integration module into its own process and drives the compositor
in-process. In the VM, Koan runs inside app_server, so WLCS cannot attach to
it. The two layers check different things:

- WLCS (and `koan_protocol_test`) check the Koan protocol layer.
- The VM checks app_server's real windows, input and pixels, with real
  clients (`weston-*`).

**Result of the last full run**, 1228 tests:

| Status | Count | What |
|---|---|---|
| PASS | 120 | |
| NOT_IMPLEMENTED | 930 | Protocols Koan does not advertise (layer-shell 368, xdg-shell v6 180, wl_shell 89, foreign-toplevel 30, viewporter 23, pointer-constraints 15, virtual-pointer 12, text-input 23, primary selection 10, …) and every touch-input variant (171). Touch variants that "pass" with a no-op touch device are counted here too, not as PASS. |
| BLOCKED | 160 | Tests that break the protocol themselves; each rule cites the spec (see below). |
| SKIPPED | 4 | gtest-disabled tests, plus WLCS's own xfail self-test. |
| FAIL | 14 | See below. |

BLOCKED rules:

- 155 tests attach a buffer before the first `xdg_surface.configure`.
  `xdg-shell.xml` requires this to be an error, and wlroots raises
  `unconfigured_buffer`.
- 3 decoration tests expect a configure before the initial commit.
- 1 test expects `orphaned` on the decoration manager. It belongs to the
  `zxdg_toplevel_decoration_v1` enum, where wlroots raises it.
- 1 test expects an error for a pool truncated after mmap.
  `wl_shm.invalid_fd` only covers a failed mmap, so this is compositor
  policy, not conformance.

**Koan fixes from WLCS findings**, each red first in `koan_protocol_test`:

| Finding | Fix | Test |
|---|---|---|
| Frame callbacks answered inside the commit, so a client that asks for a frame on every callback spins (WLCS `FrameSubmission…throttled`) | `koan_server.c`: callbacks are answered from a 16 ms timer after the tree was presented | compose `frame-callback-throttled` |
| No `wl_surface.enter` for the output (WLCS `surface_enters_output`) | `koan_server.c`: the whole tree enters the Koan output when it is presented | compose `surface-enters-output` |
| `get_xdg_surface` on a surface with a buffer: wrong error, or none for an attached-only buffer | vendored `types/xdg_shell/wlr_xdg_surface.c`: `xdg_wm_base.invalid_surface_state`, and the pending attach is checked too. Upstream 0.20.2 posts `xdg_surface.unconfigured_buffer` (3) on `xdg_wm_base`, where 3 means `invalid_positioner`. Marked as a Koan local change; report upstream | core `xdg-surface-from-{committed,attached}-buffer` |
| `set_/unset_maximized` and `set_/unset_fullscreen` never answered, so clients wait forever | `koan_adapter.c`: reply with a configure carrying the current state | xdg `toplevel-*-configures` |

The existing `koan_xdg_client_test` now waits (up to about 1 s) for frame
callbacks instead of expecting them inside the same roundtrip.

**Remaining FAIL (14)**; each one has failed in every run so far:

- 4× `XdgToplevelStableConfigurationTest.window_can_{,un}{maximize,fullscreen}_itself`:
  the state is not granted. That needs app_server's zoom/fullscreen wired
  to the request, which is bridge work (`WaylandWindow.cpp`).
- 2× `CopyCutPaste`: no selection offer when the sink gets keyboard focus.
  Not analysed yet; the data device is not bridged to the BeAPI clipboard.
- 4× `input_falls_through_subsurface_when_{,parent_}unmapped/{8,10}`
  (pointer variants), 2× `place_{above,below}_simple`, and
  1× `surface_moves_while_under_pointer`: not analysed yet. They can be
  in Koan's surface lookup or in the app_server stand-in, so that has to
  be decided before fixing anything.
- 1× `attaching_buffer_to_unconfigured_xdg_surface_is_an_error`: wlroots
  only checks at commit, while WLCS expects the error on `attach`.
  Upstream wlroots behaviour.

## 10d. Spawn regression and the NOT_IMPLEMENTED count (2026-11-11)

**"The Wayland server does not spawn on app_server."** The cause was the
image pipeline, not Koan [V]:

- The Nov 10 release ISO's `/system/servers/app_server` (2289002 bytes)
  contained no Koan code (`grep -a -c "Koan Wayland server"` = 0). In the
  VM, app_server had no `wayland dispatch` thread and `/run/user/995` had
  no `wayland-0`, although janus exported `WAYLAND_DISPLAY=wayland-0`.
- The ISO came from a tmpfs build. That tree is configured from scratch on
  every run, and `VOS_WITH_WLROOTS` defaults to OFF. Only
  `VOS_CMAKE_EXTRA_OPTS="-DVOS_WITH_WLROOTS=ON"` turned it on;
  `VOS_WITH_WLROOTS=ON` alone was not forwarded by `docker-compose.yml`
  and not read by `docker/build.sh` or `configure`.
- With the current tree's debs (wlroots ON) installed in the same VM,
  `wayland-0` exists and the dispatch thread runs.
- Fix: `VOS_WITH_WLROOTS` is forwarded, normalised and passed to
  `configure --with-wlroots/--without-wlroots`; the setting is recorded in
  `buildconfig.conf` and a mismatch with the tree's cache reconfigures it;
  `build.sh` logs the effective value.

**Why ~930 WLCS tests are NOT_IMPLEMENTED.** They are 930 *test cases*,
not 930 protocols. Run 4 (current code, `generated.wlcs.run4/`): 1228 tests,
130 PASS, 0 FAIL, 4 SKIPPED, 164 BLOCKED, 930 NOT_IMPLEMENTED. WLCS repeats
most cases for every shell and input method, so one missing global
multiplies quickly. All 930 come from 17 interfaces plus touch; every one
of these interfaces is already compiled into wlroots-koan, and Koan just
never creates the global [V]:

| Tests | Missing | Kind of work |
|---|---|---|
| 368 | `zwlr_layer_shell_v1` | Bridge: layer surfaces as anchored native windows with exclusive zones (panels, launchers, notification daemons, lock screens). |
| 180 | `zxdg_shell_v6` | Obsolete (pre-stable xdg-shell); wlroots 0.20 has no implementation. Won't fix. |
| 171 | touch (seat has no touch) | app_server has no touch input path at all. Needs touch events in app_server first. |
| 89 | `wl_shell` | Obsolete; wlroots dropped it. Won't fix. |
| 30 | `zwlr_foreign_toplevel_manager_v1` | Koan-side list of toplevels; activate/close/minimize requests go to the bridge. Would let Wayland taskbars see windows. |
| 23 | `wp_viewporter` | Create the global and honour `surface->current.viewport` in the adapter's `compose_surface`. |
| 23 | `zwp_text_input_manager_v2/v3` | Needs an input-method bridge (BeAPI input methods). |
| 15 + 3 | `zwp_pointer_constraints_v1`, `zwp_relative_pointer_manager_v1` | Pointer lock/confine in app_server (games, 3D apps). |
| 12 | `zwlr_virtual_pointer_manager_v1` | Lets any client inject pointer input. Leave off unless a use case needs it. |
| 10 | `zwp_primary_selection_device_manager_v1`, `gtk_primary_selection_device_manager` | Small: create the manager and accept `request_set_primary_selection`, like the clipboard. |
| 2 | `wp_fractional_scale_manager_v1` | Together with the `wl_output` scale work in `fractional-scaling.md`. |
| 1 | `zxdg_output_manager_v1` | Small: create it next to the output layout. |
| 1 | `xdg_wm_base` (version) | Koan advertises v3; wlroots supports up to 7. |
| 2 | `xdg_not_really_an_extension`, `wlcs_non_existent_extension` | WLCS self-checks; correct as is. |

So 269 of the 930 (`zxdg_shell_v6`, `wl_shell`) are obsolete protocols
that should be reported separately, and 539 (layer-shell, touch) need
app_server features. The rest are small or medium Koan/bridge items.

The 155 "attach before first configure" BLOCKED cases were rechecked: WLCS's
own `create_xdg_shell_stable_surface` waits for and acks the configure. The
blocked tests build their xdg surfaces by hand and attach first (e.g.
`XdgToplevelStableTest.surface_can_be_moved_interactively`,
`tests/xdg_toplevel_stable.cpp:205-208`), so the classification stands.

**Adapter.** `xdg_toplevel.set_minimized` now reaches app_server
(`koan_window_callbacks.request_minimize` → `KOAN_NATIVE_MINIMIZE`, the
minimize button's path). Test: `koan_protocol_xdg` `toplevel-set-minimized`,
red before the change. Still unwired: `request_move` / `request_resize`
(GTK client-side decorations; app_server's `DefaultWindowBehaviour` has no
entry point to start a drag or resize programmatically) and popup display
(no native popup window).

## 10e. WLCS inside the VM, against app_server (2026-11-11)

`docker/wlcs/run-wlcs-vm.sh` runs the same WLCS v1.8.1 suite inside the
debug VM, against the live session's `wayland-0`. Unlike §10c, nothing
stands in for app_server: placement, stacking, hit-testing, focus and the
per-window input path are the real ones.

How it works:

- `protocol/koan-test-v1.xml` / `server/koan_test.c`: a private
  `koan_test_v1` global with `move_surface`, `pointer_motion`,
  `pointer_button` and `sync`. app_server only creates it when the
  root-owned regular file `/run/koan-test-control` exists at startup.
- app_server (`WaylandWindow.cpp`): `move_surface` moves the native window
  (`Desktop::MoveWindowBy`). Pointer requests become `B_MOUSE_*` messages
  shaped like `LibEvdevEventStream`'s, injected with
  `EventDispatcher::InjectEvent()`.
- `sync`: queue → app_server → a `kEventStreamFence` event in the
  EventDispatcher stream → every Wayland input looper → Wayland thread. The
  `done` event is sent only after the Wayland events caused by earlier
  requests. Unanswered fences time out after 3 s and are logged; none did.
- `tests/wlcs/koan_wlcs_vm_integration.c` (`koan-wlcs-vm-integration.so`):
  a plain Wayland client module, no wlroots inside.
- Host test `koan_protocol_testctl` (4 cases) covers the Wayland half. It
  was red with the window-geometry offset removed from `move_surface`.

app_server fixes found on the way:

- `LibEvdevEventStream::InsertEvent()` never woke `GetNextEvent()`, so an
  inserted event (`kFakeMouseMoved` after a window moved, remote-desktop
  input) waited for the next real device event.
- `OutputAddonManager::_HandleInjectInput()` passed a stack `BMessage` to
  `InjectEvent()`, whose stream keeps and later deletes the pointer.

**Result** (1228 tests, 126 s, `generated.wlcs-vm/`): 105 PASS, 25 FAIL,
4 SKIPPED, 930 NOT_IMPLEMENTED, 164 BLOCKED. NOT_IMPLEMENTED and BLOCKED
match §10d exactly. Every one of the 25 FAILs passes in the in-process
run, so they are app_server (or bridge) behaviour:

| Tests | Symptom | Likely cause [S] |
|---|---|---|
| 7 `XdgToplevelStableConfigurationTest.*`, `when_parent_is_set_to_child_descendant_error_is_raised` | timeout | `ConfigurationWindow` waits for `activated` (`xdg_toplevel_stable.cpp:88`): a new Wayland window does not get activated (keyboard focus). |
| 4 `ClientSurfaceEventsTest.surface_{moves,resizes}_*under_pointer`, 4 `SubsurfaceTest.subsurface_*` and `input_seen_by_subsurface_after_parent_unmapped_and_remapped` ×2 | no enter/motion after the content changed under a still pointer | The input looper only sends motion when a mouse event arrives. The in-process stand-in re-hit-tests when content changes, app_server does not. |
| `pointer_respects_window_geom_offset`, `SurfacePointerMotionTest` corner/edge ×3, `RegionSurfaceInputCombinations.input_inside_region_seen` ×4 | wrong surface at window edges | Edge pixels of the native window go elsewhere (decorator border hit area, or the looper's inside test). |

These are not analysed further yet.

## 10f. Activation, popups and the reported app symptoms (2026-11-11)

Fixes, each red first in `koan_protocol_test` (the group is named after the
case):

| Symptom | Cause | Fix | Test |
|---|---|---|---|
| WLCS: new windows never activated (25 → 12 FAIL in the VM run) | A toplevel destroyed before its `WINDOW_READY` left its native window alive. app_server kept it under the dead toplevel's address, and a new toplevel at a reused address was refused as a "duplicate", so it was never shown. | `koan_adapter.c`: such toplevels become orphans; their late window is destroyed when `WINDOW_READY` arrives (oldest orphan per address first). | xdg `toplevel-destroyed-before-window-ready` |
| WLCS `window_can_(un)maximize/fullscreen_itself` flaky | `set_maximized` sent right after mapping reached Koan before `WINDOW_READY` and was answered with the current state. | The request is kept and passed to the window manager on `WINDOW_READY`. | xdg `toplevel-maximize-before-window-ready` |
| Chromium `--ozone-platform=wayland` exits at start | `wl_output` v4 sends `name`/`description`. Koan's output had no name, the NULL string could not be marshalled, and the client was disconnected. | `koan_server.c`: the output is named `KOAN-1`. | core `output-v4-name` |
| Nautilus hamburger menu never shows; right-click "freezes" the app | xdg_popups were configured but had no native window. The invisible menu held the pointer grab, so the next click only dismissed it. | Popups are native windows: borderless, `kMenuWindowFeel`, `B_AVOID_FOCUS`, placed relative to the toplevel's content (`create_popup`/`move_popup` callbacks, `KOAN_CMD_CREATE_POPUP`/`MOVE_POPUP`/`POPUP_READY`). Input goes to the toplevel's key in its content coordinates. Popups are dismissed (`popup_done`) when their toplevel is deactivated. | xdg `popup-window-requested`, `popup-displayed`, `popup-window-destroyed`, `popup-done-on-deactivation` |
| Menu items highlighted but clicks did nothing | `koan_pointer_focus_in()` only accepted surfaces in the toplevel's subsurface tree, so buttons, axis and leave on a popup were dropped. | It walks the toplevel with its popups (`wlr_xdg_surface_for_each_surface`). | input `pointer-button-reaches-popup` |
| app_server crashed when a session ended with a menu open | wlroots 0.20.2 `destroy_xdg_popup_grab()` destroyed its popups while they still pointed at the grab, so the grab was destroyed twice. | Local wlroots change (marked; report upstream): detach the popups first, destroy newest first. | input `shutdown-with-grabbing-popup` (was a segfault) |

Checked in the VM with the real applications:
- Nautilus: the hamburger menu and the right-click menu show, items
  activate (About, Properties) and the app stays responsive.
- Chromium (`--ozone-platform=wayland`): starts; its menu shows;
  fullscreen in and out works.
- Firefox ESR (Wayland): F11 fullscreen in and out works; its panels show.

WLCS after all of this: VM 119 PASS / 11 FAIL (the 11 are the
content-changed-under-a-still-pointer group and
`pointer_respects_window_geom_offset`); in-process 130 PASS / 0 FAIL.

Not reproduced: one Nautilus right-click whose button release reached the
client 8.5 s late, while apt was installing in the guest. The kernel event
was on time; with timing probes in the input looper and Koan, later
right-clicks went through in 100 ms.

Follow-up the same day (popups following, keyboard, outside clicks):

| Behaviour | Implementation | Test |
|---|---|---|
| A grabbing popup gets the keyboard; keys reach it; focus goes back to its parent when it closes | wlroots' popup grab ignores focus changes, so `koan_server_popup_keyboard()` calls `wlr_seat_keyboard_enter()` when the popup maps and unmaps. Focus and key handling treat the toplevel's popups as part of it. | input `popup-keyboard-to-grabbing-popup`, `popup-keyboard-keys-reach-popup` (red with the function disabled), `popup-keyboard-back-to-parent` |
| A click outside every client surface (window frame, native window, desktop) closes menus; tooltips stay | app_server `WaylandDesktopListener::MouseDown` → `KOAN_CMD_DISMISS_POPUPS` → `popup_done` for grabbing popups | input `click-outside-dismisses-grabbing-popups` |
| Popups follow their toplevel when its native window moves | `WaylandDesktopListener::WindowMoved` → `KOAN_NATIVE_FOLLOW_POPUPS` → `follow_popups()` keeps each popup at its offset in the toplevel's content | VM: `tests/wlcs/koan_popup_follow_check.c` (popup at 120,120 with the window at 100,100, and at 520,320 after the move to 500,300) |

The listener is registered under `LockAllWindows()` and never takes the
bridge lock; it only queues commands.

VM, Nautilus:
- Down, Down, Return in the hamburger menu opened Preferences.
- Escape closes the menu.
- A click on the window's title bar closes the open menu.
- No app_server crash in the whole boot.

WLCS unchanged: VM 119 / 11, in-process 130 / 0.

Still open:
- Transparent corners of client-drawn shadows and popovers show black:
  app_server draws Wayland pixels opaque.
- Firefox opens wider than the screen.
- Chromium's fullscreen exit hint is placed for the old width.

## 11. Open questions / assumptions

- [S] Whether wlroots 0.20 emits `events.commit` for an effectively synchronized
  child before or after its parent's state (decides R5).
- [S] Clipboard and DnD: no bridge to the BeAPI clipboard was found. Not traced.
- [S] Repeated create/destroy of the singleton server in one process (WLCS model).
- The xdg_wm_base version (3) is behind wlroots' maximum. Whether WLCS cases need
  a newer version was not checked.
