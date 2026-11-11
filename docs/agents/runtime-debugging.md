# Runtime debugging in a headless VM

How an agent reproduces and fixes a runtime failure (black screen, app_server
crash, a Wayland client that never shows) without a human at the QEMU window.
The goal is a **tight**, **red-capable** loop: one command that shows the
user's exact symptom, runs unattended, and takes seconds to minutes.

Scripts live in `docker/debug/`; each one documents its options in its header.

## Pick the cheapest layer that goes red

| Layer | Command | Time | Catches |
|---|---|---|---|
| Host tests | wlroots-koan tests in the builder container (below) | seconds | Wayland protocol, adapter, queue logic |
| Packages | `docker/debug/build-debs.sh` + `docker/debug/vm-install-debs.sh` | ~1 min | anything in app_server, janus, kits |
| Image | full `./podman-compose up --build` ISO (root `AGENTS.md`) | ~10 min | image assembly, first boot |

Start at the top. Drop a layer only when the bug cannot show up in the one above.

## Memory budget

The VM takes 4 GiB. Check `free -g` first and keep at least 6 GiB available:
the user may have their own VM running, and VS Code's C/C++ extension
(`cpptools-srv2`) can take ~10 GiB indexing an on-disk build tree
(`generated.*/`). An OOM kills the desktop app and every VM. Ask the user to
exclude `generated.*` from indexing rather than stopping their processes.

## Steps: VM session

1. Boot in the background, then get a root shell on the serial console:

   ```sh
   export VOS_DBG_DIR=/tmp/vosdbg        # under 108 bytes: UNIX socket limit
   docker/debug/qemu-headless.sh &       # default ISO: generated.release/output/vos-uefi.iso
   docker/debug/vm-shell.sh              # appends systemd.debug_shell=ttyS0 at GRUB
   ```

   Done when `vm-shell.sh` prints `root shell ready on serial`.

2. Drive it with `docker/debug/vmctl.py`:
   - `send 'COMMAND\r' SECONDS` runs a shell command, prints what came back;
   - `shot FILE.png` takes a screenshot (look at it, it is the user's view);
   - `hmp "sendkey ret"` presses Enter on the guest display. On the
     first-boot screen that is **Try It Now**: it starts the live session
     (user `vos-live`, uid 995; the greeter runs as `vos_login`, uid 999);
   - `move X Y`, `click X Y`, `drag X1 Y1 X2 Y2 [qcode...]` drive the
     pointer in screen pixels. The guest mouse is an absolute VMware
     VMMouse, so HMP `mouse_move` (relative) does nothing. app_server picks
     up input with a delay under load: move first, wait about a second,
     then click, and screenshot a few seconds later;
   - keys: `hmp "sendkey a"`, or modifiers held during a drag. In the live
     keymap Left Alt is Control, Left Ctrl is Command and AltGr (`alt_r`)
     or Right Win (`meta_r`) is Option, the Stack & Tile key. Stacking
     only takes when the drag moves in small steps (`drag` does): S&T
     probes the previous event's pointer position against the dragged
     title bar.

   Process names are rewritten at runtime, so `pgrep NAME` can miss a
   running server; check `/proc/<pid>` or `ps -eo pid,comm` before
   concluding it died.

3. Reproduce the symptom and capture evidence before forming a theory.

## Steps: package loop

1. Configure an on-disk tree once (the default build uses a tmpfs that
   vanishes):

   ```sh
   VOS_WITH_WLROOTS=ON VOS_CMAKE_EXTRA_OPTS="-DVOS_WITH_WLROOTS=ON" \
   VOS_BUILD_DIR=generated.release VOS_BUILD_TYPE=Release \
   VOS_TMPFS_BUILD=0 VOS_IMAGE_TYPES=none \
     ./podman-compose run -T --rm build ./docker/build.sh
   ```

   If configure fails on `gio-2.0`, `image_tree/chroot` is a stub: move it
   aside (`chroot.stub-backup-<date>`) and rerun so `setupenv.sh` builds it.

2. After each edit: `docker/debug/build-debs.sh` (incremental ninja + cpack;
   debs land in `generated.release/`, `VOS_IMAGE_TYPES=none` alone produces
   none), then `docker/debug/vm-install-debs.sh` (installs every VOS deb the
   guest already has, restarts janus). Done when it prints `DPKG_EXIT=0` and
   one `app_server`. Then `hmp "sendkey ret"` to re-enter the session.

## Steps: host tests

Build the wlroots-koan tests with the Layer 1 wrapper from
`src/add-ons/wlroots-koan/AGENTS.md`, adding `-DWLROOTS_KOAN_BUILD_TESTS=ON`,
and run `ctest --test-dir /tmp/wb --output-on-failure` in the same container.

A test counts only once you have watched it go **red**: bind-mount the old
file (`git show HEAD:path > copy`) or a deliberately broken copy over the
source with `-v copy:/vitruvian/path:ro`, see the failure, then green on the
real file.

## Steps: WLCS in the VM

With the VM from "Steps: VM session" running debs that contain
`koan_test_v1`, run `docker/wlcs/run-wlcs-vm.sh` (optionally
`-- --gtest_filter=...`). It enables test control in the guest
(`/run/koan-test-control`, root-owned), restarts the session, runs WLCS as
the session user and writes `wlcs.xml`, `wlcs.log` and `classified.txt` to
`generated.wlcs-vm/`. The full suite takes about two minutes. Test control
stays on in that guest until it reboots.

## Where the evidence is

- **app_server output**: `/var/log/app_server.log`. janus_session sends every
  server's stdout/stderr to `/var/log/<name>.log` and appends across
  restarts; the last `DrmHWInterface: janus-managed` line starts the current
  instance.
- **Crash loops**: `journalctl -b | grep janus_session`. After three
  `app_server died` within 60 s janus gives up: that is the black screen.
  `app_server.service` itself is skipped in greeter mode; janus_session
  starts the servers.
- **Aborts**: asserts are active in every build type (`NDEBUG` is never
  defined), so a wlroots assertion kills app_server with a "Guru Meditation"
  block and a glibc backtrace in its log.
- **Backtraces through libc**: the Guru backtrace stops at libc frames.
  `apt-get install -y gdb` in the guest (after `vm-install-debs.sh`, which
  keeps apt consistent) and attach `gdb -p $(pgrep -x app_server)` before
  triggering the bug; pass SIGTERM/SIGPIPE/SIGUSR* with `handle ... nostop`.
  The builder image has no gdb; debug host-test hangs with the host's gdb.
- **Wayland clients**: run them the way the desktop would, with Tracker's
  environment and the session user:

  ```sh
  T=$(pgrep -x Tracker); xargs -0 -a /proc/$T/environ sh -c \
    'exec setpriv --reuid=995 --regid=995 --init-groups env -i "$@" firefox-esr --new-instance about:blank' x &
  ```

  Add `WAYLAND_DEBUG=1` inside the `env` for the client's protocol trace.
  Guest `apt-get` works over QEMU user networking; installs vanish on reboot.
- **Probes**: app_server's wlr_log verbosity is `WLR_ERROR`, so `WLR_INFO`
  lines never show. Log temporary probes with
  `wlr_log(WLR_ERROR, "[DEBUG-xxxx] ...")` using one tag per session, and
  `grep -rn DEBUG-xxxx src/` to remove them all before finishing.
