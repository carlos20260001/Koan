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
- [ ] Import the files above (snapshot, Author Numerio)
- [ ] `src/CMakeLists.txt` and `build/profiles/*.cmake` trimmed to directories
      that exist in this tree; `src/system/CMakeLists.txt` adds `kernel`,
      `libroot`, `libroot2` only
- [ ] `build/deps.cmake` keeps only the dependencies the ported subset needs
      (udev, blkid, zlib, evdev, libinput, xkbcommon); the rest stays behind
      the same `find_package` guards it has upstream
- [ ] `./configure --build-buildtools` builds `rc`/`xres`/`resattr`
- [ ] `./configure --buildtools=buildtools --arch=amd64` then `ninja -C generated.amd64`
      configures (targets may still fail to compile until M4)
- [ ] `bake` packaging (`packages.cmake`, `data/debian/*`) produces a `vos`
      deb from whatever is installed (not run until M4 gives it content)

### M2 - Docker builder (from Koan `master`)
Files: `docker/Dockerfile`, `docker/build.sh`, `docker/run-qemu.sh`,
`docker/README.md`, `docker/debug/*`, `docker/wlcs/*` (kept, unused),
`docker-compose.yml`, `podman-compose`, `.dockerignore`, plus Koan's deltas to
`build/scripts/lib/{chroot,common,image}.sh`, `build/scripts/bake.sh`,
`build/profiles/*.cmake`, `configure` (rootless Podman, tmpfs build, ccache,
`VOS_WITH_WLROOTS` passthrough).
- [ ] Import `docker/`, `docker-compose.yml`, `podman-compose`
- [ ] Apply Koan's `build/scripts` and `configure` deltas on top of M1
- [ ] `./podman-compose build` (image builds)
- [ ] `VOS_IMAGE_TYPES=none ./podman-compose up --build` runs configure in
      the container (compile may still fail until M4)
- [ ] `docs/koan/BUILDING.md`: how to build this tree with the container

### M3 - Nexus
- [ ] Submodule added and initialised (M0)
- [ ] `src/system/kernel/CMakeLists.txt` from VitruvianOS (dkms packaging)
- [ ] `nexus-dkms` deb builds inside the container
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
- [ ] `devices/LinuxEvdevShim.h`, `devices/UdevDeviceName.h`, `devices/CMakeLists.txt` imported
- [ ] `keyboard/`: evdev + xkbcommon backend merged into the air/OS
      `KeyboardInputDevice`, `LinuxKeycodeMap.h` imported, `CMakeLists.txt`
- [ ] `mouse/`: evdev/libinput backend merged into the air/OS
      `MouseInputDevice` (keep air/OS's `movement_maker` gestures), `CMakeLists.txt`
- [ ] `src/kits/shared/PointerControlCore.cpp` (used by the mouse add-on) imported
- [ ] Both add-ons compile under CMake (needs `libbe`, `input_server` on
      CMake: **blocked until the kits are ported**, note the exact missing
      targets)
- [ ] Jam build of the two add-ons still passes (`jam -q "<input>keyboard" "<input>mouse"`)

### Not started (later milestones)
- Kits (`libbe`) and `input_server` on CMake, needed before M5 can link
- app_server on DRM, registrar, the rest of the VitruvianOS userland
- Koan's decorators, Wayland server (`VOS_WITH_WLROOTS`), WebKit bits

## Log

| Date | Commit | What |
| --- | --- | --- |
| 2026-11-15 | | M0: branch, licence appendix, tracker |
