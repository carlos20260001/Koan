# Porting VitruvianOS onto the air/OS tree

Branch: `airos-haiku-master` (air/OS `master` at `12ad0480dc`, 15 Nov 2026).
Sources: VitruvianOS `upstream/master` = `4d8c5ed719`, Koan `master` =
`c7bbbdb3ed`, Nexus = `0730b1cbed`.

Goal: the air/OS userland (Haiku R1/beta6 + air/OS) hosted on the Linux
kernel the way VitruvianOS does it: Nexus kernel modules for ports, sems and
node monitoring, `libroot2` as the libroot, evdev/libinput input add-ons,
Debian packages and ISO/RAW images built by VitruvianOS's CMake + bake
pipeline inside Koan's Podman container.

## Rules

- **Layout: in place.** VitruvianOS paths are kept verbatim so later
  `git cherry-pick -x <sha>` from `upstream/master` applies directly. Where a
  Haiku file already exists (keyboard/mouse add-ons, three `libroot/posix`
  files, shared headers) the VitruvianOS change is applied as a three-way
  merge onto the air/OS version.
- **CMake beside Jam.** Every directory that is ported gets a
  `CMakeLists.txt` (VitruvianOS's where it has one, written new otherwise).
  Jamfiles are left in place so the Jam build of air/OS keeps working; they
  are retired only once the CMake build covers the same targets.
- **Authorship.** A port is a commit whose git Author is the VitruvianOS
  author. Single commits: `git cherry-pick -x`. Snapshots (first landing of a
  component): Author = the dominant upstream author, message says
  `Ported from VitruvianOS/Vitruvian <sha>` and which paths. Every port
  carries the trailers
  `Co-Authored-By: carlos20260001 <carlosforgithub2026@outlook.com>` and
  `Co-Authored-By: Claude <model> <noreply@anthropic.com>`.
  Adaptation work (CMake trimming, merge glue) goes in separate commits of
  our own.
- **Licence.** GPL-2.0 appendix in `License.md`; the GPL text is already in
  `data/system/data/licenses/GNU GPL v2`.
- Nothing here is verified on hardware. "Builds" means the stated command was
  run and its result observed; it is written next to each checkbox.

## Milestones

### M0 - Branch, licence, tracker
- [x] `airos-haiku-master` created from `airos/master` (`12ad0480dc`)
- [x] `License.md`: GPL-2.0 appendix for VitruvianOS/Koan/Nexus code
- [x] `PORTING.md` (this file)
- [x] `ReadMe.md`: mention the appendix in the licence paragraph
- [x] Nexus submodule at `src/system/kernel/nexus` (`Numerio/Nexus` @ `0730b1cbed`)

### M1 - Debian builder (from VitruvianOS `upstream/master`)
Files: `CMakeLists.txt` (root), `configure`, `bake`, `build/*.cmake`,
`build/cross/`, `build/profiles/`, `build/scripts/bake.sh`,
`build/scripts/lib/*.sh`, `build/scripts/{runkvm,setupenv,vm-boot-recovery}.sh`,
`data/debian/`, `vos.pc.in`, `src/CMakeLists.txt`, `src/tools` (buildtools:
`rc`, `xres`, `resattr`), `.dockerignore`.
- [x] Import the files above (snapshot, Author Numerio) — `a13fa8401a`; VitruvianOS's
      `configure` lives as `vos-configure` (Haiku's Jam `configure` keeps its name and
      execs it for the CMake options); run it from inside the build dir
- [x] `src/CMakeLists.txt` and `build/profiles/*.cmake` trimmed to directories
      that exist in this tree; `src/system/CMakeLists.txt` adds `kernel`,
      `libroot`, `libroot2` only — `94c1b676c4` (guards marked "ported subset")
- [x] `build/deps.cmake` keeps only the dependencies the ported subset needs
      (udev, blkid, zlib, evdev, libinput, xkbcommon); the rest stays behind
      the same `find_package` guards it has upstream
- [ ] `./vos-configure --build-buildtools` builds `rc`/`xres`/`resattr` — FAIL:
      buildtools mode compiles 8 libroot2 files (M4), 26 `src/kits` files and 25
      shared files that differ from air/OS (`headers/build/*`, `src/bin/rc`,
      `src/build/lib{be,root}`, `src/tools/locale`); host also lacks bison/flex
- [x] `cd generated.amd64 && ../vos-configure --buildtools=<Koan buildtools> --arch=amd64 --build-type=Release`
      → "Configured correctly!"; in the `vitruvian_build` container `ninja` and
      `ninja nexus` pass (host lacks `attr`, debhelper, dkms)
- [ ] `bake` packaging produces a `vos` deb — not run; `cpack -G DEB` in the
      container already emits `VOS-0.6.3-Linux-{runtime,data,dev,buildtools}.deb`
      (runtime is empty until M4)

### M2 - Docker builder (from Koan `master`)
Files: `docker/Dockerfile`, `docker/build.sh`, `docker/run-qemu.sh`,
`docker/README.md`, `docker/debug/*`, `docker/wlcs/*` (kept, unused),
`docker-compose.yml`, `podman-compose`, `.dockerignore`, plus Koan's deltas to
`build/scripts/lib/{chroot,common,image}.sh`, `build/scripts/bake.sh`,
`build/profiles/*.cmake`, `configure` (rootless Podman, tmpfs build, ccache,
`VOS_WITH_WLROOTS` passthrough).
- [x] Import `docker/`, `docker-compose.yml`, `podman-compose` — `e0f128dc98`; the
      service is `container_name: vitruvian-airos-build` so it never starts Koan's
      `vitruvian-build` container
- [x] Apply Koan's `build/scripts` and `configure` deltas on top of M1 — `0243fdf7ee`
      (applied to `vos-configure`); `e90c04cf62` makes `docker/build.sh` call
      `vos-configure`, adds `VOS_BUILDTOOLS_DIR` and a clear stop while the
      buildtools cannot be built (M4); `VOS_WITH_WLROOTS` is inert here
- [x] `./podman-compose build` → `localhost/airos-port-docker_build` (all layers from Koan's cache)
- [ ] `VOS_IMAGE_TYPES=none ./podman-compose up --build` runs configure in
      the container — as given it stops at the buildtools step with the M4 message.
      With `VOS_BUILDTOOLS_DIR=prebuilt-buildtools VOS_TMPFS_BUILD=0
      VOS_BUILD_DIR=generated.release` (Koan's rc/xres/resattr copied in) it
      debootstraps trixie, `vos-configure` says "Configured correctly!", `ninja`
      runs its 2 attr steps, exit 0. `data/keys/` imported afterwards so
      `VOS_REPO_KEY` is no longer needed. Git is unusable inside the container for a
      worktree (`.git` is a file), so versions read `git0.unknown`
- [x] `docs/koan/BUILDING.md`: how to build this tree with the container — `10f1726d76`

### M3 - Nexus
- [x] Submodule added and initialised (M0)
- [x] `src/system/kernel/CMakeLists.txt` from VitruvianOS (dkms packaging) — in `a13fa8401a`
- [x] `nexus-dkms` deb builds inside the container — `ninja nexus` in
      `vitruvian_build` → `nexus-dkms_0.3.0+git0.0730b1c_all.deb`
- [ ] Module builds against the container's `linux-headers` (`make -C src/system/kernel/nexus/nexus`)

### M4 - libroot and libroot2 (from VitruvianOS `upstream/master`)
Files: `src/system/libroot2/` (73 files, new), `src/system/libroot/CMakeLists.txt`,
`src/system/libroot/posix/CMakeLists.txt`, VitruvianOS versions of
`libroot/posix/{send_signal.c,stat.c,user_group_common.cpp}` (exist in Haiku
at `posix/signal/`, `posix/sys/`, `posix/`), `headers/private/system`,
`headers/private/kernel` deltas, `headers/os/kernel/OS.h` and other shared
headers libroot2 compiles against (`syscalls.h`, `VRefCache.h`,
`MountInfo.h`, `fs_volume.h`, `locks.h`, `util/KMessage.h`, ...).
- [ ] Snapshot import of `src/system/libroot2/` (Author Numerio)
- [ ] `src/system/libroot/{,posix/}CMakeLists.txt`; the three posix files
      merged onto the Haiku ones (or VitruvianOS copies placed where its
      CMake expects them, if the Haiku ones cannot be shared)
- [ ] VitruvianOS-only private headers imported; shared headers get the
      VitruvianOS deltas guarded by `__VOS__` where the Haiku kernel build
      would otherwise see them
- [ ] `ninja -C generated.amd64 root` links `libroot.so`
- [ ] `src/tests/system/libroot2` (if any) run under the container
- [ ] Remaining libroot2 commits after the snapshot listed here as they are
      cherry-picked

### M5 - evdev input add-ons (from VitruvianOS `upstream/master`)
43 commits touch `src/add-ons/input_server/devices/{keyboard,mouse}`,
`devices/LinuxEvdevShim.h`, `devices/UdevDeviceName.h` (list:
`git log --oneline upstream/master -- src/add-ons/input_server/devices/keyboard src/add-ons/input_server/devices/mouse`).
air/OS has 229 of its own commits on the same add-ons (touchpad gestures,
scroll phases), so this is a three-way merge, not a copy.
- [x] `devices/LinuxEvdevShim.h`, `devices/UdevDeviceName.h`, `devices/CMakeLists.txt` imported — `665ba4e11c`
- [x] `keyboard/`: evdev + xkbcommon backend merged into the air/OS
      `KeyboardInputDevice`, `LinuxKeycodeMap.h` imported, `CMakeLists.txt` — `a2078d6b88`
- [x] `mouse/`: evdev/libinput backend merged into the air/OS
      `MouseInputDevice` (keep air/OS's `movement_maker` gestures), `CMakeLists.txt` — `9e98c0ec6b`.
      Merge shape: base is VitruvianOS's 2019 import `49ecebd96d`; every air/OS change
      since is either already in VitruvianOS or Haiku-driver specific, so each file is
      the air/OS text and the VitruvianOS text interleaved under `#ifdef __VOS__`
      (`-D__VOS__` reduces byte-for-byte to `upstream/master`, without it to air/OS).
      Follow-up: split the two backends into separate source files
- [x] `src/kits/shared/PointerControlCore.cpp` (used by the mouse add-on) imported, plus
      `headers/private/input/{PointerControlCore,XVitruvianInputProtocol}.h`,
      `headers/private/interface/PanelOrientationTransform.h`
- [ ] Both add-ons compile under CMake (needs `libbe`, `input_server` on
      CMake: **blocked until the kits are ported**). Missing targets: `be`, `shared`,
      `localestub`, `input_server`; no `src/add-ons/CMakeLists.txt` chain yet. Missing
      header deltas (each an observed error against this tree's headers):
      `headers/build/LinuxBuildCompatibility.h` (defines `__VOS__`),
      `InputServerDevice.h` (`B_GET_DEVICE_DESCRIPTION`, `UDEV_ROLE_*`...),
      `AppDefs.h` (`B_SEAT_ENABLED`...), `InterfaceDefs.h` (`B_F13_KEY`..`B_F24_KEY`),
      `private/shared/Keymap.h` (xkb lookups), `LaunchDaemonDefs.h` (`B_JANUS_SWITCH_VT`),
      `private/system/syscalls.h` (`fd_set` vs glibc). Hazard: VitruvianOS's
      `private/input/keyboard_mouse_driver.h` redefines `KEY_*` as evdev codes; must
      come with the kits under `__VOS__`. Syntax check with `-D__VOS__` against
      VitruvianOS's headers: 0 errors in all 6 files
- [ ] Jam build of the two add-ons still passes — not run (no cross tools here).
      Evidence: `cpp`-equivalence of every merged file without `__VOS__` to the
      air/OS original; Jamfiles untouched; `Keyboard.rdef` keeps
      `x-vnd.Haiku-KeyboardInputServerDevice` (`25ceefa95a`'s tree-wide signature
      rename is a separate decision; the VOS path launches `x-vnd.vos-Terminal`)

### Not started (later milestones)
- Kits (`libbe`) and `input_server` on CMake, needed before M5 can link
- app_server on DRM, registrar, the rest of the VitruvianOS userland
- Koan's decorators, Wayland server (`VOS_WITH_WLROOTS`), WebKit bits

## Log

| Date | Commit | What |
| --- | --- | --- |
| 2026-11-15 | `0ff2aa2a09` | M0: branch, licence appendix, tracker |
| 2026-11-15 | `654269f633` | M1: CMake/Debian pipeline merged (`port/debian-builder`); buildtools blocked on M4 |
| 2026-11-15 | `d31169c430` | M5: evdev add-ons merged (`port/evdev`); CMake link blocked on the kits |
| 2026-11-15 | `ce44a5947b` | M2: Podman build container merged (`port/docker-builder`); pipeline reaches configure with prebuilt buildtools |
