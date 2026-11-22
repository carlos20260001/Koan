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
- [x] Nexus submodule at `src/system/kernel/nexus` (`Numerio/Nexus` @ `0730b1cbed`);
      since 2026-11-17 the Koan fork `carlos20260001/Nexus`, branch `koan` (see M3)

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
- [x] `./vos-configure --build-buildtools` builds `rc`/`xres`/`resattr` — fixed by M9 W1-K1; was FAIL:
      buildtools mode compiles 8 libroot2 files (M4), 26 `src/kits` files and 25
      shared files that differ from air/OS (`headers/build/*`, `src/bin/rc`,
      `src/build/lib{be,root}`, `src/tools/locale`); host also lacks bison/flex
- [x] `cd generated.amd64 && ../vos-configure --buildtools=<Koan buildtools> --arch=amd64 --build-type=Release`
      → "Configured correctly!"; in the `vitruvian_build` container `ninja` and
      `ninja nexus` pass (host lacks `attr`, debhelper, dkms)
- [x] `bake` packaging produces a `vos` deb — done in the M9 W4 ISO run (the debs and `vos-uefi.iso`); was not run; `cpack -G DEB` in the
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
- [x] Module builds against the container's `linux-headers` (`make -C src/system/kernel/nexus/nexus`)
      — Debian 6.12.117 `rt-amd64` and `amd64`: `nexus.ko`, `area.ko`,
      `nexus_node_monitor.ko`, 0 warnings, before and after the fork's patch
- [x] **Fork** (user decision, 2026-11-17): `carlos20260001/Nexus`; `main` mirrors
      `Numerio/Nexus`, `koan` = main + Koan patches, merged from upstream before the
      pointer moves. Patch `71e5929`: `NEXUS_THREAD_WAITFOR` honours `flags`/`timeout`
      (`calculate_timeout()`, boottime clock; a zeroed request waits as before).
      Submodule switched in `7c67bf5ea3`
- [ ] Runtime test of the timed wait with the module loaded (QEMU)

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
- [x] Buildtools build since M9 W1-K1 (the `packages.cmake` posix-subset part is still open); was failing at
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
- Userland that M6 deleted with `src/add-ons/kernel` before the "port userland"
  rule (2026-11-16), to restore when its Linux counterpart is ported: the PPP
  userland library `network/ppp/shared/libppp` (ppp_up/pppconfig; →
  ModemManager/pppd), the userlandfs server and netfs server/shared code (→ FUSE).
  All recoverable from `d61de85d04^`
- Known fallout (Jam only, nothing CMake builds): 73 userland files include
  kernel add-on sources/headers (BFS tools, disk systems, network bits). They
  are replaced when VitruvianOS's versions of those directories are ported.
- Resolved in M9 W1-K1: the `#ifdef __VOS__` branches M4 added to shared headers
  (`OS.h`, `Errors.h`, `SupportDefs.h`, ...) keep their Haiku halves. They can
  be resolved the same way once nothing builds the Haiku side

### M7 - air/OS boot splash on Linux (replaces Plymouth)
Port the original Haiku splash code, not a Plymouth theme. VitruvianOS
images install no Plymouth: GRUB passes `quiet splash` and nothing draws
until app_server starts. This milestone fills that gap.

Sources (kept in `src/system/boot_splash/`, see its README): the boot loader's
`video_splash.cpp` (decompress images, draw logo and empty icon row) and the
kernel's `boot_splash.cpp` (light one icon per stage); headers in
`headers/private/kernel/` (`boot_splash.h`, `boot/platform/generic/video_splash.h`,
`video_blitter.h`); image headers from `src/tools/generate_boot_screen.cpp`.

**Artwork** (2026-11-20): the Koan wordmark (`data/artwork/boot_splash/koan_splash_logo.svg`)
above air/OS's stage icons, `headers/private/kernel/boot/images-koan.h`, the default
`VOS_SPLASH_IMAGES` (`cff8afefe1`); regenerate with
`src/system/boot_splash/tools/make-koan-images.sh` (rsvg-convert + the opt-in
`generate_boot_screen` target). air/OS's `images-airos.h` still builds.
**Trademarks:** only Koan and air/OS artwork. The Haiku logo images
(`splash_logo*`, `images-tm*.h`) are Haiku trademarks and must never ship.

Merged in `b92bdc0170` (`port/splash`).

- [x] `vos-splash` — `389ac07399`: static binary (~860 KB, `ldd`: not dynamic), the
      original placement, blitter and zlib decoding with their copyright headers;
      platform layer = DRM dumb buffer through raw ioctls (no libdrm) with `DIRTYFB`
      flushes, `/dev/fb0` fallback (8-32 bpp), clean exit 0 when neither exists.
      Trademark guard: `VOS_SPLASH_IMAGES` defaults to `images-airos.h`; configure
      stops on `images.h`, `images-tm*.h`, `images-sans-tm.h` (checked)
- [x] Stage control — `196d2de0b1`: daemon on `/run/vos-splash.fifo`,
      `--stage N`, `--quit`, `--lit N`; seven Linux boot points
- [ ] initramfs-tools hook + `init-top`/`init-bottom` and systemd stage units —
      `b407da8963`: files, drop-ins and install rules (`build/profiles/splash.cmake`)
      in place; `systemd-analyze verify` and `sh -n` clean; not yet run under real
      `mkinitramfs` and systemd
- [ ] Handoff: app_server side done in W3-A (`quit` to the FIFO after the first CRTC
      set); not yet run end to end. Shown in QEMU (`SET_MASTER` EBUSY before stage 7, succeeds after,
      picture kept). The daemon keeps running across the root switch like plymouthd
      (directory fds for `/dev` and `/run`), reopens when efifb/simpledrm is
      replaced by the GPU driver, and a `vos-splash.service --lit 2` fallback covers
      boots where the initramfs had no frame buffer. app_server side still to do:
      after its first `drmModeSetCrtc` it runs `/usr/libexec/vos-splash --quit`
      (W3). Flicker-free hand-off not checked on a real boot
- [x] Packaging: install rules checked with `cmake --install` (runtime + data);
      `bake` not run
- [ ] Verify: host tests `vos_splash_test` 56 checks, 0 failed (`ctest` 1/1); manual
      QEMU (KVM, OVMF, `virtio-vga` and `virtio-gpu-pci`, busybox initramfs,
      `switch_root`) drew the splash and all seven stages. Not done: full image
      boot via `docker/run-qemu.sh`, `nomodeset`, a real GPU
- Known limits: first connected connector only, no HiDPI scaling, fbdev 32 bpp
  assumes XRGB, no LUKS password prompt; the Debian initramfs has no GPU drivers,
  so BIOS boots without a VESA mode show the splash only from the real root

### M8 - VitruvianOS partitioner (DriveSetup) and installer
The air/OS DriveSetup/Installer drove Haiku's kernel disk device manager and
installed a package-based BFS system; neither exists on Linux. They are
replaced by VitruvianOS's versions (`upstream/master` `4d8c5ed719`), which plan
partition changes in the storage kit and apply them through root helpers.
Merged in `b563cfb440` (`port/installer`).

- [x] Backend (no libbe) — `f2fa21dd71`: `data/libexec/{vos-drivesetup-helper,
      vos-drivesetup-query,vos-install-helper,vos-partition-lib.sh,vos-firstboot-commit}`,
      polkit `org.vitruvian.{drivesetup,drivesetup-query,installer}.policy` +
      `49-vitruvian-installer.rules`, `data/etc/default/grub`, `data/etc/grub.d/15_vos`,
      `data/etc/installer/excludes.list`, `data/share/vos/user-default-groups`.
      Install rules active without the rest of `data.cmake` (`fff162b85b`);
      `cmake --install` puts every file where upstream does. `sh -n`/`bash -n` and
      `xmllint` clean; shellcheck: only benign warnings
- [ ] Simulated install harness `src/apps/installer/tests/sim/` — 101 pass, 5 fail;
      the 5 also fail upstream: VitruvianOS `36ce11e906` lets the helper replace a
      fallback loader carrying its own `installed-by-vos` marker, the older harness
      still expects a refusal. `fuzz.py`: 54 cases, 0 failures. `docker/Dockerfile`
      gained `xfsprogs btrfs-progs grub2-common` (`a699a52d77`)
- [x] libroot2 disk layer (`fs/disk_device.cpp`, `disk_monitor.cpp`) — M4
- [x] Storage kit `src/kits/storage/disk_device/` (+ `jobs/`, `PartitionPlanBuilder`)
      is VitruvianOS's — `2d9c4ef3c3`; `headers/private/storage/{DiskDevice,
      MutablePartition,Partition}.h` replaced (source-compatible additions); 28/28
      files syntax-clean. Its `CMakeLists.txt` waits for the `be` target
- [x] `src/apps/drivesetup`, `src/apps/installer` are VitruvianOS's — `cf30dd9ff9`,
      catalogs `4edb366ddc`, `src/apps/CMakeLists.txt` with per-app target guards
      (`23a3491234`), Jamfiles retired (`9cb2f55635`), note in
      `docs/airos/INSTALLER.md` (`837190118b`). Trademark: DriveSetup keeps air/OS's
      `kLeaf` boot marker instead of VitruvianOS's Haiku-leaf drawing.
      Dropped air/OS-only features: whole-disk GPT+ESP+BFS install and its disk menu
      (`39ee76cd20`), retrying partitioning (`61b9f68707`), `*.starting` marker skip
      (`2bf484d225`), air/OS logo in the Installer window (`23a002ea9e`), the
      `UninitializeContents()` crash fix (`70f9d2cb37`, its source is gone); also
      Haiku 2025-26 DriveSetup/Installer features merged into air/OS (disk image menu,
      type change, EFI loader install/copy, optional packages)
- [x] polkit agent `src/apps/vos-polkit-agent`, `vos-polkit-grant-helper` +
      `data/systemd/vos-polkit-agent.service` — `6a1e42973c`
- [ ] Apps link: **blocked until the kits (`be`, `tracker`, `shared`, `translation`,
      `localestub`) are on CMake**. Syntax check: DriveSetup 13/15, Installer 7/8 clean;
      the rest need upstream deltas in `Bitmap.h` (`<cstddef>`), `RosterPrivate.h`
      (`LogOut`, 4-arg `_ShutDown`), `private/shared/Keymap.h` (xkb lookups, also M5),
      `LaunchDaemonDefs.h` (`B_JANUS_SESSION_LOCKED`); the grant helper needs
      `libpam0g-dev` from the chroot sysroot
- Notes: `build/packages.cmake` now gates `data.cmake` on `data/systemd/janus.service`
  (the directory exists for the agent unit alone). Jam leftovers:
  `src/kits/storage/Jamfile` lists deleted `disk_device` sources;
  `src/tests/apps/installer/Jamfile` was already broken

### M9 - Whole tree on CMake, no Jamfile left (decision 2026-11-16)
Every one of the 924 Jamfiles goes. Policy decided by the user:
- **Directories both trees have (215: kits, servers, most apps/preferences):**
  merge air/OS into VitruvianOS. VitruvianOS's Linux-hosted sources are the base;
  air/OS's changes since the common Haiku ancestor are merged on top (features
  kept, Linux mechanics from VitruvianOS). CMakeLists is VitruvianOS's, extended
  for air/OS sources.
- **Haiku-kernel-only air/OS directories:** only Haiku kernel modules and
  drivers are deleted, except `src/add-ons/accelerants` and
  `src/add-ons/kernel/drivers/graphics`, which get CMakeLists behind an option
  that defaults to OFF (sources untouched).
- **Userland that talks to Haiku kernel stacks is ported, not deleted** (user,
  2026-11-16): `src/add-ons/disk_systems` (→ blkid/libfdisk/mkfs),
  `src/add-ons/network_settings` and `src/bin/network/*` (→ netlink /
  NetworkManager), net-stack and bluetooth-stack userland (→ Linux sockets,
  BlueZ), `src/bin/makebootable` and `src/tools/makebootable` (→ GRUB/EFI).
- **Other air/OS-only directories** (apps, add-ons, kits that are userland):
  CMakeLists written new; ported to Linux.
- **Tests:** port those that build on Linux (CTest); delete tests of deleted code;
  record the rest.

Merge method (per file in a shared directory): base = the Haiku revision the
VitruvianOS file started from (blob match in this tree's Haiku history, else the
closest), with the `x-vnd.vos` rename applied to the base; air/OS unchanged since
base → VitruvianOS's file; VitruvianOS unchanged → air/OS's file; otherwise
`git merge-file` and hand resolution. Merge commits carry the VitruvianOS
author, list the air/OS commits kept, and both trailers.

Waves (each on its own `port/*` branch, merged here when green):
- [x] W1-K1 — merged `3d55d7c27b` (`port/kits-core`): 280 headers in scope (76 imported,
      124 air/OS, 56 VitruvianOS, 23 merged, incl. `OS.h`, `SupportDefs.h`,
      `Screen.h`, `Window.h`, `Bitmap.h`, `InterfaceDefs.h`, `ServerProtocol.h`,
      `PrivateScreen.h`, `syscalls.h`); the M4 `__VOS__` halves resolved (283 → 5
      uses, all VitruvianOS's own). Display API kept air/OS's (`DeviceScale`,
      `WaitForOutputRetrace`, scan timing; VitruvianOS rotation/temperature/connector
      codes appended to `ServerProtocol.h`, no renumbering). support, app, storage
      kits merged (`b8bc382c82`, `9177f8562f`, `23ebf4e18d`); real `be_bootstrap`;
      buildtools merged (`73d3399c11`), attributes `user.beos.*` as libroot2/Nexus.
      Integrated check on the merged tree (container): `./vos-configure
      --build-buildtools` → this tree's `rc`/`xres`/`resattr`/`linkcatkeys`;
      configure with them → "Configured correctly!"; `ninja root be` →
      `libroot.so`, partial `libbe.so` (84 undefined symbols, all W2 interface/
      locale/icon); `ninja nexus` → deb. TODO(air/OS merge) spots: `OS.h`
      (`debug_printf`), `syscalls.h`, `PrivateScreen.h` (`BaseAddress`/`BytesPerRow`),
      `cpu_type.h`, `LooperList.cpp` (no `__rw_lock_*` in libroot2),
      `ServerMemoryAllocator.cpp` (no `_kern_unreserve_address_range`); air/OS's
      `wait_for_thread_etc()` declared but not in libroot2 (done in `7e4fd44e54`). Not done: kit unit tests
      (cppunit not in tree; standalone ones need W2). 65 differing headers left for
      W2+ (media, midi, bluetooth, net, game, device, print, tracker, agg, cppunit)
- [x] W1-C: cleanup — merged `c774b58282` (`port/cleanup`). **No userland deleted**
      (user rule); every candidate kept for a later wave with its Linux counterpart:
      `disk_systems` → blkid/libfdisk/mkfs via `PartitionPlanBuilder`;
      `network_settings`, `servers/net`, `bin/network/*` → NetworkManager
      (VitruvianOS `libnetapi/NMBackend`, ModemManager), iproute2/netlink;
      `system/libnetwork` → glibc + nss-mdns; `libs/compat` → nl80211/OpenSSL;
      bluetooth server/add-ons/tools → BlueZ (VitruvianOS `BlueZBackend`);
      makebootable/bootmanager/writembr → GRUB/EFI; debugger/debug kit/debug_server →
      ptrace, `/proc`, perf; bfs/fat shells → FUSE; `libs/nvmm` → KVM; Haiku image and
      hpkg tools → bake/Debian. GPU: `VOS_WITH_HAIKU_GPU_DRIVERS` (OFF) in
      `src/add-ons/CMakeLists.txt` + `HaikuGpu.cmake`; all 55 accelerant/graphics
      directories have CMakeLists, Jamfiles gone, no source changed (`340df28d89`,
      `d06a6a13ae`). OFF: configure + `ninja root` green. ON: configure green; 8
      accelerants and `amdcopytest` compile, kernel drivers cannot (no Haiku kernel);
      7 accelerants wait for `be`; `intel_extreme/kernel_cpp.cpp` came from the
      deleted kernel. Tests of kernel code deleted (`0817ae4a6e`: system
      boot/runtime_loader/glue, add-ons/kernel except `graphics/dp_sink`, kernel
      net-stack shells, kernel tests VitruvianOS lacks); the libroot userspace tests
      were restored (`b30650969d`). Jamfiles: 923 → 766
- [x] W2: `src/kits/{interface,locale,translation,shared,tracker,...}`, `localestub`;
      `be`, `tracker`, `translation`, `shared`, `localestub` link
      - [x] W2-A merged `2d885506c7`: interface kit merged (air/OS DeviceScale,
            WaitForOutputRetrace, scan timing, multi-monitor kept; VitruvianOS rotation,
            reflection, temperature, connector, change-confirm, grabs, fire-and-forget
            MoveTo/ResizeTo on top; no air/OS feature dropped), locale (system ICU),
            libicon (VitruvianOS supersampling + air/OS density icons), agg, linprog,
            alm; `localestub` defined; libbe embeds air/OS `Icons.rdef`. `be` links;
            left undefined: 5 libshared symbols (HSL, EmojiTables, DragTrackingFilter).
            CTest 2/2. `src/CMakeLists.txt` adds userland dirs only where a CMakeLists
            exists; new `VOS_BUILD_TESTS` option. For W3: app_server must not reply to
            AS_WINDOW_MOVE/RESIZE and must handle AS_ACQUIRE/RELEASE_POINTER/KEYBOARD_GRAB
      - [x] W2-B merged `159105bf61`: shared, tracker, translation merged;
            textencoding VitruvianOS's (glibc iconv); screensaver sources identical.
            Tracker: VitruvianOS base (vref model, XDG trash, DisksWindow, statx) with
            all air/OS features (tabs, sidebar, details pane, breadcrumb/search, Quick
            Look and thumbnails, tags, grouping, folder sizes, batch rename, network
            shares, per-monitor background, scaling fixes, air/OS artwork; 31 jmgasper
            commits). Merge bases capped at VitruvianOS's last Tracker sync (~hrev59405)
            because "closest blob" silently dropped air/OS changes. `KeyCommandMap.cpp`
            taken from VitruvianOS for libtracker (`0b84da96fc`; KeyInfos waits for W4);
            build image gained libssl-dev. Not checked at runtime: air/OS network-share
            mounting still talks to Haiku's mount server
      - [x] W2-C merged `9aa147d50c`: VitruvianOS has no classic Media Kit (media2 on
            PipeWire/GStreamer/fluidsynth is a different API), so both live side by
            side: air/OS's classic `libmedia` (unchanged API, Nexus ports/areas to
            media_server) for its hardware decoders, MediaConverter, audio device
            choice, A2DP, UVC; VitruvianOS's `libmedia2` imported verbatim, used by
            game, midi, midi2. 1009 clashing symbols separated by version nodes
            (`MEDIA_1`, `MEDIA2_1`; CTest `media_symbol_versions`); `UseMediaKit()`
            puts the classic headers first. game merged (BDirectWindow: air/OS
            device_scale/drawing bits + VitruvianOS shared-event handshake; evdev
            Joystick, termios SerialPort); midi/midi2 VitruvianOS's (+ air/OS
            `MidiSettings`); opengl, hwaccel imported; media_controls, neural (air/OS)
            on CMake. Integrated: `libmedia`, `libneural`, `libhwaccel`,
            `libmedia_controls` link; `libmedia2` waits for `shared` (W2-B); libgame for
            the merged `Joystick.h` (W2-D). Later: a PipeWire node add-on for
            media_server (replaces `hmulti_audio`, W3/W4); midi_server redundant;
            consider putting `headers/os/media` (not media2) on the global path in W4
            and prefixing the ~9 unprefixed media2 includes. libroot2 lacks
            `wait_for_thread_etc` (libneural). Build image gained media2/GL dev packages
      - [x] W2-D merged `333350f20b`: network merged on VitruvianOS's NetworkManager/
            ModemManager backends (air/OS LinkSpeed, CA-bundle fallback,
            `BNetworkRoute` from `/proc/net/*route`, AutoConfigure via NM kept;
            route add/remove and `AddPersistentNetwork(password)` TODO(linux backend));
            Bluetooth: air/OS API kept on BlueZ (`GetBluetoothAddress()` → `bdaddr_t`,
            VitruvianOS's string form renamed `GetBluetoothAddressString()`), AES from
            OpenSSL; TODO(linux backend): friendly name, authentication, controller
            properties, ATT/LE pairing, A2DP AVDTP, LE HID (BlueZ HOG + uhid covers it);
            device: VitruvianOS USB kit (libusb/udev) + air/OS A2D/D2A/DigitalPort/
            AuxDisplay, isochronous stubs; package: both kits (`libvpackage` apt,
            `libpackage` hpkg as target `bpackage`; libsolv add-on skipped, Debian has no
            `repo_haiku`); print utilities on CMake (`pr_server.h` constants kept);
            quicklook, mail ported; debug/debugger on CMake behind `VOS_WITH_DEBUG_KIT`/
            `VOS_WITH_DEBUGGER_KIT` (OFF, ptrace port pending); accounts, location,
            telephony, web and libs backlight, kdbx, libscan, outputaddon, prefs,
            printcups, xclient imported. CTest backlight 2/2. Follow-ups done here:
            build image deps (`f9e259f415`), air/OS rumble/device-ID API on evdev
            (`8baad50179`), duplicate device Joystick/SerialPort dropped (`c3c748229a`),
            `BPrivate::SHA256` restored into libroot (`985d5b8116`)
      - **W2 integrated result** (container, full `ninja -k 0`): no failed target;
            24 shared libraries, all 0 undefined symbols except libneural (1:
            `wait_for_thread_etc`, fixed in `7e4fd44e54` on the Nexus fork: now all
            24 link with 0 undefined symbols). Jamfiles: 712
      - Integrated check after A+B+C (`c87706e750`, container): configure green;
            `libroot`, `libbe`, `libmedia`, `libmedia2`, `libmidi`, `libmidi2`,
            `libtranslation`, `libtextencoding`, `libhwaccel`, `libalm` link with 0
            undefined symbols. Waiting: libgame (merged `Joystick.h`, W2-D) and through
            it libtracker, libscreensaver, libopengl; libneural has 1 undefined symbol,
            `wait_for_thread_etc`: fixed — Nexus fork patch `71e5929` (timed
            `NEXUS_THREAD_WAITFOR`) + libroot2 `7e4fd44e54` (relative → one deadline,
            `B_CAN_INTERRUPT`, timed pidfd fallback for other teams)
- [x] W3: `src/servers/app` on a new Linux display backend (design below),
      registrar, input_server, the other servers
      - [x] W3-B merged `15f4ad3df0`: KMS/GBM/Mesa backend in
            `drawing/interface/linux/kms/` (new, Koan, MIT), plugged in through W3-A's
            `create_kms_hw_interface()` (NULL → drm → fbdev). See the design section below
            for what is done. Unit tests 8780 checks, 0 failed (modes, EDID, layouts, no
            seams at 1.0/1.25/1.5/1.75, cursor hot spot, damage, CPU resample). QEMU, two
            virtio-gpu heads (`kms_harness`, 40/40 each): CPU dumb/GBM/legacy/double-
            buffered, GPU pass on llvmpipe and on virgl. Finding kept from air/OS: at a
            1:1 fractional scale a region overlaps its neighbour by ≤1 column, never a gap.
            Runtime switches `VOS_KMS_GPU/BUFFERS/ATOMIC/SLOTS/PLANE_SCALING/SEAT/DEVICE`.
      - **Wave 3 integrated** (container, `VOS_BUILD_TESTS=ON`): full `ninja -k 0` exit
            0; 33 server/test binaries and 26 shared objects, 0 undefined symbols;
            app_server links libdrm, gbm, EGL, GLESv2, udev, seat; ctest 9 passed, 1
            skipped (`pipewire_graph`, no PipeWire in the container). Not yet run:
            app_server on any display (next: QEMU smoke test)
      - [x] W3-A merged `7e3d6143e8`: app_server merged (251 shared files: 162
            identical, 19 VitruvianOS, 38 air/OS, 32 by hand; bases capped at ~hrev59405).
            air/OS kept: ScreenManager/VirtualScreen/DisplayLayout/MultiLocker (on
            libshared's BRWLocker), render scale, `display_output` API, per-output retrace
            and flush threads, ScaledReadback, colour glyphs, ligatures, fractional/grey-AA
            text, Painter exact-pixel, decorators, DirectWindow device pixels. VitruvianOS
            on top: DRM/fbdev/evdev backends, DisplayChangeTracker, VirtualOutput,
            OutputAddonManager, PowerManager, rotation/reflection/temperature/connector,
            grabs. ScreenRelayout runs inside air/OS's `_MoveWindowsWithDisplays`. No reply
            to AS_WINDOW_MOVE/RESIZE; `"display_frames"` sent to input_server. Interim
            DRM/fbdev got Koan's `rework/scaling` render-scale commits (one output, id 0).
            Backend choice `ScreenManager::_CreateLinuxHWInterface()`: kms → drm → fbdev,
            `VOS_DISPLAY_BACKEND` overrides; KMS plugs in via `create_kms_hw_interface()`
            and `kms/CMakeLists.txt` (`APP_SERVER_KMS_SRCS`). Splash handoff: `quit` to
            `/run/vos-splash.fifo` after the first CRTC set (M7). TODO(air/OS merge):
            in-place growth of client memory (a8b31c4d09). Accelerant/remote interfaces not
            built (sources kept). Not run on a display yet
      - Integrated (`7e3d6143e8` + root CTest fix): full `ninja -k 0` exit 0; 11 server
            binaries and 25 libraries, all 0 undefined symbols; ctest 8/8 passed
            (`VOS_BUILD_TESTS=ON`). Jamfiles: 698
      - [x] W3-D merged `b59b7785d9`: all 20 air/OS servers on CMake, none deleted.
            media_server + media_addon_server (classic Media Kit) with a new PipeWire
            node add-on (`src/add-ons/media/media-add-ons/pipewire`: default and
            per-device sinks/sources as BBufferConsumer/Producer over `pw_stream`; not
            a time source yet; CTest `pipewire_graph`, skips without PipeWire; smoke
            test on the host played 24000 frames through a null sink). bluetooth_server
            = BlueZ `org.bluez.Agent1` pairing agent (HCI/SDP/HID host built, not
            started); net_server keeps saved networks/auto-join/services on
            NetworkManager (configure-interface/resolver/service-order
            B_NOT_SUPPORTED); power_daemon on UPower + logind inhibitor locks +
            power-profiles-daemon; print_server on CUPS (queues as spool folders);
            syslog_daemon reads the journal; debug_server shows crash dialogs from
            systemd-coredump; location_server asks GeoClue first; mdns_server binds
            5353 with SO_REUSEPORT beside Avahi; accessibility_server stays resident;
            index, mail, mcp, osd, quick_look, print_addon, media_controls ported;
            midi_server and package_daemon build, unused. TODO(linux backend): AT-SPI,
            MPRIS, ptrace debugging, RTC wake-ups. Must run: media, media_addon,
            bluetooth, syslog, debug, print servers (systemd/janus_session start still
            to wire). Runtime deps: pipewire, wireplumber; optional coredump, UPower,
            power-profiles-daemon, GeoClue. Not yet checked in the container: the last
            accessibility_server edit and `ldd -r` (host podman broke 20:22-~21:00 after a
            system update's AppArmor reload; fixed by reloading the podman profile).
            `src/servers/Jamfile` retired. Jamfiles: 677
      - Integrated after A+C+D (`16c4e9f0e4`, container, `VOS_BUILD_TESTS=ON`): full
            `ninja -k 0` exit 0; 31 server binaries and 26 shared objects (incl. the
            PipeWire add-on) all 0 undefined symbols; ctest 8 passed, 1 skipped
            (`pipewire_graph`: no PipeWire in the build container; passed on the host)
      - [x] W3-C merged `3a9c5062bf`: registrar merged (VitruvianOS logind/sleep,
            legacy-signature alias, X/Qt roster + air/OS quit-user-apps, sync per
            phase); input_server merged (VitruvianOS seat/udev, orientation, no reply
            to `IS_SCREEN_BOUNDS_UPDATED` + air/OS `_ConstrainToDisplays`, which needs
            app_server's `"display_frames"`); mount_server merged (udev/libmount +
            air/OS NetworkShares, SMB through `mount.cifs`, password via `$PASSWD`; not
            run against a server); notification merged (D-Bus bridge + air/OS views);
            keystore VitruvianOS's; janus/janus_session imported; air/OS launch_daemon
            builds but is not installed (janus answers its port). janus_session starts
            air/OS's media_controls, osd, quick_look, accessibility, location, mdns,
            power and mcp servers at login (no on-demand start). systemd units for
            janus and the core servers in `data/systemd`, installed by a
            `packages.cmake` fallback (`data.cmake` still names 78 missing files).
            All 8 server binaries link with 0 undefined symbols (after libroot2's
            `get_driver_settings_string` signature fix `967edb6e52`); they stop at
            "Can't open Nexus IPC" without the module. Registrar test programs built,
            not run. Runtime deps to add: cifs-utils (Recommends)
- [ ] W4: `src/apps`, `src/preferences`, `src/add-ons`, `src/bin`, `src/libs`,
      **Exception (user, 2026-11-20): Deskbar and the Tracker app/add-ons use no
      VitruvianOS source** — air/OS's code ported to Linux by hand (VitruvianOS read
      only as a reference for mechanisms), committed under our authorship.
      - [x] data port merged (`port/w4-data`): VitruvianOS's session, systemd, PAM,
            helpers, /etc fragments and /system/boot imported; welcome,
            makefile-engine, profile, SetupEnvironment merged; mime_db merged (189
            types, air/OS's 19 extra kept; Summit stays preferred browser) and built by
            CMake; keymaps compiled by a new host `keymap` tool; `data.cmake` active again
            (ported-subset fallback gone; licenses still ship); kits that installed to
            `/system/lib` (mail, package, quicklook, web) now install to `/lib`, and
            postinst copies a real `/system/lib` before relinking it (a first install
            deleted them); first_login unpacks the E-mail provider infos; postinst names
            air/OS's Office/Media/Developer/Utilities folders. **Release ISO builds**:
            `vos-uefi.iso`, 1.3 GB (sha256 `3fdf8117db88…`), `vos` configures with no
            postinst skips, Nexus DKMS module for 6.12.117-rt in the image. Not
            installed yet: classic `libmedia.so` (media_server needs it), Wi-Fi firmware,
            Haiku `data/settings`, Mail dictionaries; janus starts the greeter path, which
            needs FirstBootPrompt / a login app (not ported yet). Not booted yet
      - **Apps (user, 2026-11-21): the 63 remaining apps use air/OS sources only**, like
            Deskbar/Tracker/preferences; `src/apps/CMakeLists.txt` adds every app
            directory with a CMakeLists
      - [x] **All 63 remaining apps on CMake** (air/OS sources only, six parallel ports,
            merged 2026-11-21): applets (10), menu programs (22), demos (10, + Cortex's
            add-ons), system apps (19). Integrated: 96 app/preference binaries, 0
            undefined symbols; ctest 10/10; 60 apps in `/system/apps`, 67 Deskbar menu
            links, none broken; `src/apps` Jamfile-free. Skipped behind guards:
            Debugger/DebugAnalyzer (ptrace back end), WebPositive (HaikuWebKit). Login is
            janus's `vitruvian-login` through a `/system/servers` link; FirstBootPrompt
            is janus's first-boot frontend. Back ends: PowerStatus → UPower, Devices →
            sysfs/libudev, Terminal → Linux ptys, ProcessController power saving →
            power-profiles-daemon, MediaConverter → FFmpeg + VA-API/MPP. Open
            TODO(linux backend): BootManager install (GRUB helper), Devices "disable
            driver" (modprobe blacklist helper), Pulse CPU toggle (root helper),
            package install/update (libsolv add-on + package_daemon), Mail live queries
            (libroot2 B_QUERY_UPDATE), camera input (V4L2 media add-on), Iris/Cedar/NVENC
            encoders, `debug_thread()`; WebPositive bookmarks first-login unpack;
            branding strings "Welcome to Haiku"/"Quit Haiku" in Login/FirstBootPrompt
      - [x] Tracker app + 8 add-ons (`bd442cd7d5`), Deskbar + menu data
            (`f01071d9b2`), all 26 preference panels (P1 `port/w4-prefs-ui`, P2
            `port/w4-prefs-sys`, `17be2fda96`), air/OS sources only; VitruvianOS-only
            panels (accounts, camera, hardware, location, user) left out (user,
            2026-11-21). Integrated: full build exit 0, apps and panels 0 undefined
            symbols, ctest 10/10, 26 panels in `/system/preferences`, 25 Deskbar
            Preferences links, none broken. Linux back ends: Time → timedated,
            VirtualMemory → `/proc/swaps`, WiFi → wireless-extension ioctls, Network →
            primary interface by UDP-socket source address, Joysticks → evdev nodes,
            E-mail → `/etc/resolv.conf`, ScreenSaver → libxcrypt. Open:
            `BNetworkDevice::GetNetworks()/GetNetwork()` still stubs in libnetapi (WiFi
            list empty); VirtualMemory `Apply()` needs a polkit root helper; nl80211
            details (MCS, width); Joysticks links named after `eventN` (not stable);
            nothing run on a display yet
      `src/data`, `data/`, remaining tests
- [ ] W5: `Jamrules`, `Jamfile`, `build/jam`, Haiku `configure`, Haiku-only
      `build/scripts` deleted; `git ls-files | grep -c Jamfile` = 0; full `ninja`,
      `bake` ISO, QEMU boot to the desktop

### M9-W3 design - Linux display backend for app_server (decision 2026-11-16)
VitruvianOS's display code (`drawing/interface/linux/{drm,fbdev}`) drives one
screen, has no fractional scaling and no GPU acceleration. It is **not** the
base for app_server's display side. air/OS's architecture is: `ScreenManager`,
`VirtualScreen`, `DisplayLayout` (several monitors in one desktop, mirroring,
hot-plug), one `HWInterface` per monitor, per-monitor `RenderScale` (100-250 %,
scaling only at rasterisation, app_server/input_server logical; see
`docs/airos/SCALING.md`), flushes at each output's vertical blank
(`docs/airos/RETRACE.md`). A custom backend wires that to the Linux
counterparts and Mesa:

- [x] **KMS device** (`drawing/interface/linux/kms/`, new): one per `/dev/dri/cardN`;
      DRM master through the session (reuse VitruvianOS's VT/logind/seat handling);
      atomic modesetting; enumerates connectors, CRTCs and planes; udev hot-plug
      events feed `ScreenManager` add/remove the way air/OS's monitor hot-plug does
- [x] **KMS output = HWInterface** per connector (as built: one HWInterface per card, every
      connector a `display_output`, air/OS's accelerant model): modes from the connector, EDID
      through `decode_edid` into air/OS's monitor info; `SetRenderScale`;
      `WaitForRetrace` and tear-free flushes from page-flip events; hardware cursor
      on the cursor plane at the render scale; per-output gamma/brightness where KMS
      offers it. Reuse VitruvianOS's `DrmBuffer`, `BufferAgeTracker`,
      `DrmFlipPolicy` and Koan's `rework/scaling` work on `DrmHWInterface`
      (render scale, cursor plane at render scale, refuse software scale) instead of
      rewriting them
- [x] **Scan-out buffers:** GBM buffer objects (`libgbm`), dumb buffers as fallback;
      fbdev stays only as a last-resort single-screen fallback
- [x] **GPU acceleration through Mesa** (EGL on GBM, GLES2/3) — composition/upload and
      fractional resample done (display-engine scaling first, then GLES, then CPU);
      still open: `BGLView` zero-copy dmabuf import, fill/blit hooks (HWInterface has none):
      - composition/flush: damaged regions of the CPU (`Painter`/AGG) back buffer
        uploaded and drawn to the scan-out BO by GLES, which also does the
        fractional-scale resample; CPU path kept when EGL is unavailable;
      - accelerant-style 2D hooks (fill, blit, scaled blit) on GLES;
      - OpenGL kit: `BGLView` renders with Mesa EGL on the render node
        (`/dev/dri/renderD*`) into dmabuf-backed buffers that app_server imports
        (texture or overlay plane), zero-copy, replacing VitruvianOS's
        `mesa_surfaceless` read-back (kept as fallback); GPU choice through
        VitruvianOS's `GlDeviceSelect`/hwaccel settings;
      - VA-API video decode for air/OS's media add-ons belongs to W4
- [x] Build deps: `libgbm-dev`, `libegl-dev`, `libgles-dev`, `libdrm-dev` (container
      and chroot); Mesa from Debian; all optional at configure time with the CPU
      path as fallback
- [ ] Tests: QEMU two heads done for the backend (40/40 x6); open: app_server on it,
      udev hot-plug event (QEMU sent none; re-probe works), VT switch/resume, a real GPU.
      Originally: QEMU with two heads (`virtio-gpu-gl`/`virtio-vga-gl`, `max_outputs=2`,
      virgl) and a llvmpipe/no-GPU run; scales 1.0/1.25/1.5/1.75 with hit-test and
      seam checks (AGENTS.md); hot-plug add/remove; mirroring; VT switch and resume
- Koan's Wayland server (`VOS_WITH_WLROOTS`) and the DRM display layout it needs
  come after this, on the same KMS device

### M10 - Desktop completeness — pending (user, 2026-11-21; after the running W4 tasks)
The Deskbar menu (`build/profiles/deskbar.cmake`, air/OS's lists) links only programs
this tree builds, so its folders fill as ports land.
- [ ] **Applications, Utilities, Demos, Preferences:** port the remaining programs the
      menu lists (`deskbar.cmake`): Applications (DeskCalc, StyledEdit, Terminal,
      HaikuDepot, Icon-O-Matic, Mail, MediaConverter, MidiPlayer, People,
      SoftwareUpdater, SoundRecorder, System Updater), Utilities (CharacterMap,
      Debugger, Devices, DiskProbe, DiskUsage, Expander, CodyCam, Magnify, PoorMan,
      SerialConnect; DriveSetup done), Demos (Chart, Clock, Cortex, FontDemo, GLTeapot,
      Lookout, Mandelbrot, OverlayImage, Pairs, PatchBay, Playground, Pulse, Sudoku);
      Preferences are running (P1/P2)
- [ ] **Desktop applets:** port the Deskbar status applets `src/apps/{wifistatus,
      bluetoothstatus,networkstatus,powerstatus,processcontroller,workspaces,autoraise,
      launchbox,switcher,screenreader}`, Pulse's `DeskbarPulseView`, `src/bin/desklink`
      (volume), the KeymapSwitcher (`src/preferences/keymap`), and the media-controls and
      notification replicants (`src/servers/{media_controls,notification}`). Deskbar runs
      without them; `first_login/default_deskbar_items.sh` adds the installed ones
- [ ] **Office, Media, Developer:** air/OS fills these from external packages: its image
      pulls prebuilt `.hpkg` packages from HaikuPorts (`build/jam/repositories/HaikuPorts`),
      and each package supplies its own menu link (`build/jam/packages/Haiku`: "Office,
      Media and Developer applications supply their links in their own packages").
      Plan: add the recipe sources as git submodule(s) (HaikuPorts recipes and/or the
      `airos-works/*` forks, to be chosen), and a **bypass** in the build so those recipes
      produce **Debian `.deb` packages, not `.hpkg`**, built in the container/chroot,
      each installing its menu link under `/system/data/deskbar/menu/{Office,Media,
      Developer}` and published through the vos apt repository. To decide: which
      repository and which packages per folder
- [ ] Also open from the shell ports: `src/preferences/deskbar` link name (`Deskbar_prefs`)
      and `Tracker_prefs` must match the preferences ports; the `menu_entries` type
      attribute on the system file comes from `postinst`

### M10 - air/OS Mesa (hgl + Zink) (user, 2026-11-22)
`BGLView` gets air/OS's hardware route instead of VitruvianOS's `mesa_egl`/
`mesa_surfaceless`: Mesa's Haiku platform (hgl) with air/OS's Zink renderer add-on,
built on Linux and run on Debian's Vulkan drivers (RADV, ANV, NVK; lavapipe on the
CPU). The kernel side stays Linux DRM; nothing here touches DRM or accelerants.

**Source (5).** air/OS's series (`docs/x399-workstation/patches/mesa-hgl/0001..0016`,
already in this tree) is documented as applying to "X547/mesa's Haiku branch at
`71177f7`". That commit is not published: X547/mesa has no such object (GitHub API
422, `git fetch` of the short sha fails), nor does freedesktop Mesa; air/OS's commit
`f28f179972` says one of the two trees "has no remote at all". The series' base is
Mesa **22.0.5** (`docs/x399-workstation/STATUS.md`, 2026-09-18: the add-on is built
"from that same release" as Haiku's mesa package, 22.0.5; patch 0001's
`src/gallium/meson.build` preimage `a678dc0` is exactly 22.0.5's blob), and all 16
patches apply with `git am` onto the `mesa-22.0.5` tag commit `18f91b5895af`, which
X547/mesa does serve. So `3rdparty/mesa` is a shallow submodule of
`https://github.com/X547/mesa.git` pinned at `18f91b5895af` (= upstream
`mesa-22.0.5`); nothing is forked or pushed. The build copies it (no `.git`) into
the build tree and applies air/OS's series, then Koan's
(`docs/x399-workstation/patches/mesa-hgl-koan/0001..0003`) with `patch -p1`.

**Haiku-specific pieces and what they need (1).**

| Mesa piece | Needs from the OS | Here |
| --- | --- | --- |
| `src/hgl` -> libGL (BGLView, BGLRenderer, GLRendererRoster, GL entry points through glapi) | libbe (BView, BWindow, BDirectory, BPath, find_directory), libroot (load_add_on, get_image_symbol, `_kern_get_safemode_option`, get_system_info), `DirectWindowPrivate.h`, `safemode_defs.h` | all present (libroot.so exports them; headers in `headers/private`) |
| `include/HaikuGL` GLView.h / GLRenderer.h | - | Mesa's own copies are used. `headers/os/opengl/GLView.h` has the same layout; `headers/private/opengl/GLRenderer.h` is VitruvianOS's different ABI (dispatcher argument, GetGLProcAddress slot), used only by `src/kits/opengl` |
| `frontends/hgl` (st_api glue, bitmap_wrapper) | BBitmap, translation kit | libbe, libtranslation |
| `winsys/sw/hgl` | BBitmap; with air/OS's 0006+ the direct-window scan-out target | libbe |
| `targets/haiku-zink` (air/OS 0001-0016) -> add-on `Zink` | BGLView private `fClipInfo` (member-pointer trick), BDirectWindow incl. air/OS's `MarkDrawingBitsStale`, `direct_buffer_info::device_scale`, `DIRECT_BUFFER_INFO_AREA_SIZE`, BScreen (`ColorSpace`, `WaitForRetrace`), ColorConversion.h, libgame | all present (air/OS headers; `ldd -r` of the probe's `libzinkpipe.so`: no undefined symbol); `WaitForRetrace` is served by the KMS backend's retrace semaphore (`KmsHWInterface::RetraceSemaphore`) |
| `drivers/zink` + air/OS 0004/0005 (GPU copy into the window system's memory via `VK_EXT_external_memory_host`) | a Vulkan driver | Debian `libvulkan1` + `mesa-vulkan-drivers` |
| `drivers/zink` + air/OS 0006-0015 scan-out (`VkImportScanoutMemoryHAIKU`, `hgl_scanout_target`) | NVK on Haiku's nvrm publishing the accelerant frame buffer | missing on Linux, compiled out (`__HAIKU__`) |

**meson gate and the Linux-host patch (2).** The platform is chosen with
`-Dplatforms=haiku` (`with_platform_haiku`), not by `host_machine.system()`, so it
configures on Linux; what breaks is the SDK lookup (`find_library('be'/'game'/
'translation'/'network')`, hard-coded `/boot/system/develop/headers/private`) and
name clashes with Koan's headers. Koan's series, applied after air/OS's:
- `0001 meson: build the Haiku platform on a non-Haiku host` - options
  `haiku-sdk-args`/`haiku-sdk-link-args` (our include dirs, `-D__VOS__=1`,
  `-include LinuxBuildCompatibility.h`; `-lbe -lgame -ltranslation -lroot`), no
  libnetwork, and `haiku-lib-suffix` (`-hgl`) for libGL/libglapi; Haiku keeps the
  old lookups;
- `0002 util, hgl: build against the BeAPI headers of Koan` - `u_debug.h` takes
  Haiku's `debug_printf` branch under `__VOS__`, Mesa's `thread_id` becomes
  `util_thread_id` (4 uses), Koan's `node_ref::node()` accessor;
- `0003 zink: no frame buffer to present into outside Haiku` - a
  `zink_represent_into_scanout()` that returns false when `__HAIKU__` is unset.
Total 13 files, about 90 lines: a modest series, so the checkpoint did not stop here.
Probe (scratch, before wiring): `meson setup -Dplatforms=haiku
-Dgallium-drivers=zink,swrast ...` + `ninja` in the `airos-haiku-master_build`
image against this tree's headers and `generated.release` libraries: PASS
(923 ninja steps); `libGL-hgl.so.1`, `libglapi-hgl.so.0`, `libzinkpipe.so` and
`libswpipe.so` link, and `ldd -r` of `libGL-hgl` and `libzinkpipe` reports nothing
undefined. Mesa 22.0.5 builds with Debian 13's
gcc 14, Python 3.13, meson 1.7.

**Library names (3).** Decision: **Mesa's hgl libGL provides `BGLView` and the
renderer roster**, exactly the ABI air/OS's add-on was written for, as on Haiku and
air/OS. It installs as `/lib/libGL-hgl.so.1` (+ `/lib/libglapi-hgl.so.0`), so it
never shares a soname with Debian's glvnd `libGL.so.1` (X11/Wayland clients, the
app_server's EGL/GLES compositor) and they never meet in one process unless a
BeAPI program also loads glvnd. BeAPI programs link `GL-hgl` (CMake interface
target), which gives them `BGLView` *and* the GL entry points, so `glClear` in
GLTeapot or Terminal-OpenGL dispatches into Mesa 22's glapi, not glvnd's (which
would have no context). The renderer add-on installs as
`/system/add-ons/opengl/Zink` (air/OS's name; `GLRendererRoster` looks in
`<add-ons>/opengl` from `find_directory`). It is the only one installed:
the roster instantiates the first add-on it lists, directory order is undefined on
Linux, and the Zink add-on already falls back to softpipe inside itself
(`HGL_NO_ZINK`, or no Vulkan device). VitruvianOS's `src/kits/opengl`
(`libopengl`, its loader, `GlDeviceSelect`) stays in the tree and its tests still
build, but leaves the image when `GL-hgl` exists: its loader would call the Zink
add-on with the other `BGLRenderer` ABI. `mesa_egl`/`mesa_surfaceless` are not
ported (removed from `OPENGL_RENDERERS`). Packaging: component `mesa-hgl` ->
`vos-mesa-hgl` .deb (Depends: `vos (= version)`, `libvulkan1`,
`mesa-vulkan-drivers`, `libdrm2`, `libexpat1`, `zlib1g`, `libzstd1`, libc/libstdc++);
bake installs every .deb of the build, so the image gets it. Risk kept: a BeAPI
program that also loads glvnd's libGL gets two GL dispatchers; Mesa 22's glapi
symbols are `-Bsymbolic` inside libGL-hgl/libglapi-hgl, but this is untested.

**Present paths (4).** Kept on Linux: frame into a BBitmap and `DrawBitmap`
(winsys), and air/OS 0004/0005's GPU copy into the window system's memory through
`VK_EXT_external_memory_host` where the Vulkan driver offers it (RADV/ANV/NVK do;
whether app_server's shared bitmap memory imports on Linux is untested), else the
CPU copy. Haiku-only, compiled out with a false-returning stub: air/OS 0006-0015's
"present a direct window into the screen itself" (`VkImportScanoutMemoryHAIKU`,
NVK on nvrm importing the accelerant frame buffer; `hgl_scanout_target`; 0008 frame
buffer replacement; 0010/0012 display waits and moving windows; 0015 scaled
scan-out). Those would need a Linux counterpart (dmabuf of the KMS scan-out BO or an
overlay plane from app_server), which is follow-up work. vsync through
`BScreen::WaitForRetrace` is kept and reaches the KMS backend's retrace semaphore.

- [x] Design (this section)
- [x] Submodule, series, build step, `vos-mesa-hgl` .deb (merged `0f159065ab`). Builds:
      `VOS_TMPFS_BUILD=0 VOS_BUILD_DIR=generated.release VOS_BUILD_TYPE=Release
      VOS_IMAGE_TYPES=none ./podman-compose up --build` in the `port/opengl` worktree
      exits 0 (6491 ninja steps, "Mesa hgl: ON (Mesa 22.0.5, 19 patches)", no
      `OPENGL_RENDERERS` drop line); `LC_ALL=C.UTF-8 cpack -G DEB` writes
      `VOS-0.6.3-Linux-mesa-hgl.deb` (`vos-mesa-hgl`: `/lib/libGL-hgl.so.1`,
      `/lib/libglapi-hgl.so.0`, `/system/add-ons/opengl/Zink`); every NEEDED of those,
      GLTeapot and Terminal-OpenGL is in the chroot or a vos/vos-mesa-hgl library, and
      `ldd -r` reports none unresolved; ctest 1 passed, 1 skipped (pipewire_graph),
      1 not run (vos_splash_test, not built in Release). After a checkout:
      `git submodule update --init 3rdparty/mesa`, else the build leaves Mesa hgl out
- [ ] Runtime: GLTeapot on RADV/ANV/NVK hardware, and on QEMU (virtio-gpu has no
      Vulkan, so lavapipe or softpipe)

### Not started (later milestones)
- Kits (`libbe`) and `input_server` on CMake, needed before M5 can link
- app_server on DRM, registrar, the rest of the VitruvianOS userland
- Wayland server (`VOS_WITH_WLROOTS`), WebKit bits
- Koan's decorators: done in `7ad58622d7` except KoiPopDecorator and
  ModernTabDecorator (need Koan master's app_server tab strip and rounded
  corners: `Decorator::HasTabStrip`/`SetDraggedTab`/`GetCornerCutouts`/
  `SetSquareCorners`, `HIGHLIGHT_HOVER`, `TabStripLayout`, `CornerShape`,
  DefaultWindowBehaviour tab dragging; left out of `DECORATORS`). Also open:
  KoiPop as the default decorator/control look (`0d889ed467`) and the KoiPop
  light colour scheme as default (`74118dddbf`), which would replace this
  tree's light defaults (`GetSystemColor`) under its light/dark switch; the
  KoiPop dark scheme (`23d8128466`) is not ported by decision (2026-11-22),
  the control look follows `ui_dark_mode()`. Not run on a display

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
| 2026-11-16 | `b563cfb440` | M8: VitruvianOS DriveSetup, Installer, backend and polkit agent merged; apps wait for the kits |
| 2026-11-16 | `b92bdc0170` | M7: original splash on Linux (DRM/fbdev, initramfs, systemd stages) merged |
| 2026-11-16 | `c774b58282` | M9 W1-C: GPU trees on CMake behind an OFF option; userland kept; kernel tests removed |
| 2026-11-16 | `3d55d7c27b` | M9 W1-K1: headers, support/app/storage kits, buildtools merged; libbe (partial) links, buildtools from this tree |
| 2026-11-16 | `2d885506c7` | M9 W2-A: interface/locale kits merged, be complete except libshared |
| 2026-11-16 | `9aa147d50c` | M9 W2-C: classic Media Kit + media2 side by side; game, MIDI, GL, hwaccel, neural kits |
| 2026-11-17 | `159105bf61` | M9 W2-B: shared, Tracker, translation, textencoding, screensaver merged |
| 2026-11-17 | `333350f20b` | M9 W2-D merged; W2 complete: all kits on CMake, 24 libraries link |
| 2026-11-17 | `7c67bf5ea3` | Nexus forked (`carlos20260001/Nexus` `koan`), timed thread wait; `wait_for_thread_etc` in libroot2; libneural links |
| 2026-11-20 | `3a9c5062bf` | M9 W3-C: registrar, input_server, mount, notification, keystore, janus; launch_daemon links |
| 2026-11-20 | `7e3d6143e8` | M9 W3-A: app_server merged on interim DRM/fbdev; full tree builds, ctest 8/8 |
| 2026-11-20 | `b59b7785d9` | M9 W3-D: the other 20 servers on Linux backends, PipeWire media add-on; integrated build green (31 servers, 26 libs, ctest 8+1 skipped) |
| 2026-11-20 | `72f99e7e6b` | License audit of the 1183 VitruvianOS-derived files: all license/copyright headers kept; license texts now installed; License.md appendix corrected |
| 2026-11-20 | `15f4ad3df0` | M9 W3-B: KMS/GBM/Mesa display backend merged; wave 3 code complete, integrated build green, ctest 9+1 skipped |
| 2026-11-20 | `cff8afefe1` | Splash shows the Koan wordmark (images-koan.h, generator on CMake) |
| 2026-11-21 | `1c42a01c50` | Release ISO: chroot-only compile fixes (`<utility>` in netservices2, `std::isnan`/`isfinite` in app_server); postinst skips missing data |
| 2026-11-21 | `bd442cd7d5` | W4: Tracker app + 8 add-ons from air/OS sources (no VitruvianOS code) |
| 2026-11-21 | `f01071d9b2` | W4: Deskbar + its menu data from air/OS sources; logind suspend/hibernate, lock, log out |
| 2026-11-21 | `17be2fda96` | W4: all 26 preference panels from air/OS sources; src/preferences Jamfile-free |
| 2026-11-21 | `84a0729c50` | W4: all 63 remaining apps on CMake from air/OS sources; 96 binaries 0 undefined; src/apps Jamfile-free |
| 2026-11-21 | `41fda17b21` | **Release ISO with every app**: vos-uefi.iso 1.38 GB (sha256 `2815a423fb3b…`), 60 apps, 26 panels, Deskbar, Tracker, Login (vitruvian-login link), FirstBootPrompt, splash, Nexus area/nexus/nexus_node_monitor for 6.12.117-rt; vos configures cleanly. Not booted yet |
| 2026-11-21 | `594475e1b6` | W4: data/ and src/data ported, data.cmake active; **Release ISO builds** (vos-uefi.iso, 1.3 GB) |
| 2026-11-22 | `0f159065ab` | M10: air/OS's Mesa route merged (`port/opengl`): Mesa 22.0.5 + air/OS Zink series + Koan series -> `vos-mesa-hgl` (libGL-hgl, Zink add-on on Debian Vulkan); compile-only Release build and cpack green; not run on a display |
| 2026-11-22 | `7ad58622d7` | Decorators and control looks on CMake (`port/decor`): Be/Flat/Mac/Win decorators and Be/Flat control looks from air/OS sources + VitruvianOS CMake, Koan's ModernDecorator and KoiPopControlLook (dark mode from the system); KoiPopDecorator/ModernTabDecorator open; Jamfiles of decorators, control_look, input_server devices/filters removed. Compile-only Release build: ninja exit 0, `ldd -r` of the 8 add-ons 0 unresolved (decorators against app_server's exports), ctest 10 passed, 1 skipped; not run on a display |
