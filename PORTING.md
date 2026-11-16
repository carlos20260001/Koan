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
- **CMake replaces Jam, directory by directory.** Every directory that is
  ported gets a `CMakeLists.txt` (VitruvianOS's where it has one, written new
  otherwise). Decision of 2026-11-16: the target is Linux + Nexus only, so a
  directory's Jamfile is deleted as soon as CMake builds its replacement, and
  the parent Jamfile stops including it. Directories whose CMake side does
  not build yet keep their Jamfile until it does.
- **No Haiku kernel.** `src/system/kernel` holds only Nexus and its
  `CMakeLists.txt` (2026-11-16). Jam targets that compiled kernel sources
  (boot loader, runtime loader, kernel add-ons/drivers, some tests) no longer
  build; what to do with them is decision D1 below.
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
      `configure` lives as `vos-configure` (Haiku's Jam `configure` keeps its name);
      always call `vos-configure` directly, from inside the build dir. The
      dispatcher in Haiku's `configure` (`cc1673711d`) was reverted in `f1ac7fa3c6`
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
- [x] Snapshot import of `src/system/libroot2/` (Author Numerio) — `e5d7ee766a`
- [x] `src/system/libroot/{,posix/}CMakeLists.txt`; the three posix files —
      `4c197f871e`, `4c60665ee7`: `send_signal.c` (identical to Haiku's) and
      `stat.c` (Linux rewrite) placed at `libroot/posix/`, Haiku's copies untouched;
      `user_group_common.cpp` merged under `__VOS__` (registrar port by `find_port`)
- [x] VitruvianOS-only private headers imported (`fc3e2d248d`, `ae9bc680d3`);
      29 shared headers/sources merged under `__VOS__` (`6e9e18da96`): `OS.h`,
      `Errors.h` (host errno table), `SupportDefs.h`, `Message.h`, `Roster.h`,
      `Entry/Node/Statable/StorageDefs.h`, `Locker.h` + `Locker.cpp` (BLocker on a
      user_sem), `syscalls.h`, `KMessage.h`, `directories.h`, ... `gcc -E` without
      `__VOS__` is identical to air/OS for all 29. `build/defs.cmake` defines
      `__VOS__=1` globally; `headers/posix` (Haiku libc) is replaced on the include
      path by a 7-file overlay of VitruvianOS's posix headers (`22e59bfe34`).
      Merge bases were the closest Haiku revisions (histories share no commit)
- [x] `ninja root` links `libroot.so` — `[70/70] Linking CXX shared library
      src/system/libroot.so` in `vitruvian_build` (host lacks libudev headers);
      undefined symbols only glibc/libstdc++/udev/blkid. Links against a
      `be_bootstrap` subset (List, PointerList, String, StringList, Flattenable,
      Locker, UserSemaphore) in new `src/kits/{,support/}CMakeLists.txt` (`dadf6629b1`)
- [ ] libroot tests — upstream has none for libroot2; `src/tests/system/libroot/os/system_watching_test`
      hand-linked against `libroot.so` runs to "Can't open Nexus IPC" (needs nexus.ko;
      its `Test()` macro needs `be`)
- [ ] Remaining libroot2 commits after the snapshot listed here as they are
      cherry-picked — none yet (snapshot is at `4d8c5ed719`)
- [ ] Buildtools: `vos-configure --build-buildtools` still fails at
      `src/build/libbe/storage` (`src/kits/storage/sniffer/CharStream.cpp` missing);
      `packages.cmake` still installs all of `headers/` (needs the 7-file posix subset)

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
- Jam build of the two add-ons — moot: their Jamfiles were retired in M6
      (`c96ff2222f`). Signatures follow the tree-wide `x-vnd.vos-*` rename (M6)

### M6 - Retire Haiku-only code (decision 2026-11-16)
- [x] Haiku kernel sources deleted (612 files), `src/system/kernel` keeps
      `CMakeLists.txt` + `nexus` — `564cc23cd0`
- [x] Jamfiles retired where CMake builds the replacement: all 78 under
      `src/system/libroot`, `devices/keyboard`, `devices/mouse`; parents updated — `c96ff2222f`
- [x] After both: `vos-configure` "Configured correctly!", `ninja root` →
      `libroot.so`, `ninja nexus` → `nexus-dkms_*.deb` (container)
- Kept for now (CMake side not building yet): `src/bin/rc`, `src/build/*`,
  `src/tools/locale`, `src/kits/support` Jamfiles
- [x] Call `./vos-configure` directly; Haiku `configure` dispatcher reverted — `f1ac7fa3c6`
- [x] Adopt VitruvianOS `25ceefa95a`: every `x-vnd.Haiku`/`x-vnd.haiku` token →
      `x-vnd.vos` in 5421 files, six `mime_db/application/x-vnd.haiku-*` types
      renamed — `4385f4ca1b`. `x-vnd.Be-*` untouched; the two `.patch` files for
      external trees left as they are; no length-based prefix checks found;
      `libroot.so` still builds. No install migration (`vos-signature-migrate`
      not ported: no existing installs)
- [x] D1: Haiku-kernel-only trees deleted, each replaced by its Linux/VitruvianOS
      equivalent — `d61de85d04`: `src/system/boot` (→ GRUB + bake images),
      `runtime_loader` (→ glibc `ld.so`), `glue` + `ldscripts` (→ glibc crt,
      toolchain link scripts), Haiku libroot sources (→ libroot2; only
      VitruvianOS's 5 files remain under `src/system/libroot`),
      `src/add-ons/kernel` (→ Linux + Nexus). **Kept untouched:**
      `src/add-ons/kernel/drivers/graphics` (GPU drivers, ported Linux DRM pieces)
      and `src/add-ons/accelerants`. Splash sources moved to
      `src/system/boot_splash/` for M7
- [x] D2: keyboard/mouse keep only the Linux path — `252ff7be13`; the 8 files and
      `libroot/posix/user_group_common.cpp` are byte-identical to VitruvianOS
      `4d8c5ed719`; `movement_maker.*` and its `touchpad_scroll_test` deleted
- Known fallout (Jam only, nothing CMake builds): 73 userland files include
  kernel add-on sources/headers (BFS tools, disk systems, network bits). They
  are replaced when VitruvianOS's versions of those directories are ported.
- Still open: the `#ifdef __VOS__` branches M4 added to shared headers
  (`OS.h`, `Errors.h`, `SupportDefs.h`, ...) keep their Haiku halves. They can
  be resolved the same way once nothing builds the Haiku side

### M7 - air/OS boot splash on Linux (replaces Plymouth) — planned
Port the original Haiku splash code, not a Plymouth theme. VitruvianOS
images install no Plymouth: GRUB passes `quiet splash` and nothing draws
until app_server starts. This milestone fills that gap.

Sources (kept in `src/system/boot_splash/`, see its README): the boot loader's
`video_splash.cpp` (decompress images, draw logo and empty icon row) and the
kernel's `boot_splash.cpp` (light one icon per stage); headers in
`headers/private/kernel/` (`boot_splash.h`, `boot/platform/generic/video_splash.h`,
`video_blitter.h`); image headers from `src/tools/generate_boot_screen.cpp`.

**Trademarks:** only the air/OS artwork (`data/artwork/boot_splash/airos_splash_*.png`,
`headers/private/kernel/boot/images-airos.h`). The Haiku logo images
(`splash_logo*`, `images-tm*.h`) are Haiku trademarks and must never ship.

- [ ] `vos-splash`: a small static userspace program built from the two
      sources. Replace the boot loader framebuffer and `kernel_args` with a DRM
      dumb buffer (`/dev/dri/card0`, the same technique as app_server's
      `linux/drm/modeset.c`) with fbdev (`/dev/fb0`) as fallback; keep the
      blitter and zlib image decoding as they are
- [ ] Stage control: `vos-splash --stage N` (or a FIFO the daemon reads) maps
      Haiku's seven `BOOT_SPLASH_STAGE_*` to Linux boot points: initramfs
      start, root mounted, `sysinit.target`, udev settled, `basic.target`,
      registrar up, app_server about to take the display
- [ ] initramfs-tools hook + `init-top` script so the logo shows before the
      root mount; systemd units (`vos-splash-stage@.service`) for the later stages
- [ ] Handoff: the splash drops DRM master and exits when app_server starts
      (app_server's DRM backend must succeed in `drmSetMaster` right after);
      no flicker to the text console in between; `quiet splash` only, the
      verbose/recovery GRUB entries keep the text console
- [ ] Packaging: install in the image through `build/profiles` + `bake`; CMake
      target under `src/system/boot_splash/CMakeLists.txt`
- [ ] Verify in QEMU (`docker/run-qemu.sh`, virtio-gpu DRM and `nomodeset`
      fbdev) and on one real GPU; tests for the stage-to-icon mapping and the
      image decoding on the host

### M8 - VitruvianOS partitioner (DriveSetup) and installer — in progress
The air/OS DriveSetup/Installer drive Haiku's kernel disk device manager and
install a package-based BFS system; neither exists on Linux. They are
replaced by VitruvianOS's versions (`upstream/master` `4d8c5ed719`), which plan
partition changes in the storage kit and apply them through root helpers.

Layers, bottom up:
- [ ] Backend (no libbe): `data/libexec/{vos-drivesetup-helper,vos-drivesetup-query,
      vos-install-helper,vos-partition-lib.sh}`, `data/polkit-1/actions/org.vitruvian.{drivesetup,drivesetup-query,installer}.policy`,
      `data/polkit-1/rules.d/49-vitruvian-installer.rules`, `data/etc/default/grub`,
      `data/etc/grub.d/15_vos`, with their install rules
- [ ] Simulated install harness `src/apps/installer/tests/sim/` (sparse-file disks,
      no loop devices) runs green in the container
- [x] libroot2 disk layer (`fs/disk_device.cpp`, `disk_monitor.cpp`) — already in M4
- [ ] Storage kit `src/kits/storage/disk_device/` (+ `jobs/`, `PartitionPlanBuilder`)
      replaced by VitruvianOS's version; headers it needs
- [ ] `src/apps/drivesetup`, `src/apps/installer` replaced by VitruvianOS's
      versions (CMakeLists, rdef, catalogs `data/catalogs/apps/{drivesetup,installer}`);
      air/OS-only features dropped and listed here
- [ ] polkit agent apps `src/apps/vos-polkit-agent`, `vos-polkit-grant-helper`
      (the password prompt the helpers need)
- [ ] Apps link: **blocked until the kits (`be`, `tracker`, `shared`, `translation`,
      `localestub`) are on CMake**

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
| 2026-11-15 | `3a28f25a0b` | M4: libroot2 merged (`port/libroot2`); `libroot.so` links in the container |
| 2026-11-15 | | Integrated check on the merged tree (`vitruvian_build` container, Koan buildtools mounted): `vos-configure` "Configured correctly!", `ninja root` → `libroot.so` (620680 bytes), `ninja nexus` → `nexus-dkms_0.3.0+git0.unknown_all.deb` |
| 2026-11-16 | `c96ff2222f` | M6: Haiku kernel and the replaced Jamfiles deleted; root + nexus still build |
| 2026-11-16 | `4385f4ca1b` | Dispatcher reverted (`vos-configure` only); signatures renamed to `x-vnd.vos-*` |
| 2026-11-16 | `252ff7be13` | D1: Haiku-kernel-only trees deleted (graphics drivers, accelerants kept); D2: Linux-only input add-ons; M7 splash planned |
