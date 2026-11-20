# Boot splash: vos-splash

The original Haiku boot splash, ported to Linux (milestone M7 in
`PORTING.md`).

## Sources

- `video_splash.cpp` - the boot loader side (was
  `src/system/boot/platform/generic/video_splash.cpp`): decompresses the
  images and draws the logo and the grayed out icon row.
- `boot_splash.cpp` - the kernel side (was `src/system/kernel/boot_splash.cpp`,
  deleted with the Haiku kernel in `564cc23cd0`): lights one icon per stage.
- Their headers stay in `headers/private/kernel/`: `boot_splash.h`,
  `boot/platform/generic/video_splash.h`, `video_blitter.h`.

The drawing logic (placement, blitter, zlib decoding) is Haiku's. What
changed is the platform layer: `kernel_args` and the `FRAME_BUFFER_BOOT_INFO`
boot item became a `splash_frame_buffer` (`splash_platform.h`) that
`splash_display.cpp` fills from

1. a DRM dumb buffer on the first `/dev/dri/card*` with a connected
   connector (raw ioctls, XRGB8888; the technique of VitruvianOS app_server's
   `linux/drm/modeset.c`), or
2. `/dev/fb0` (8, 15, 16, 24, 30 and 32 bpp, as the blitter handles), or
3. nothing: `vos-splash --daemon` then exits 0 at once.

`vos_splash.cpp` is the program around it, `splash_stages.*` the Linux boot
points. It links statically against glibc and zlib (no libbe, no libroot), so
the initramfs hook copies one file.

## Artwork

The default is the Koan wordmark above air/OS's stage icons:
`headers/private/kernel/boot/images-koan.h`, made by
`tools/make-koan-images.sh` from `data/artwork/boot_splash/koan_splash_logo.svg`
(rendered onto black at 420 px wide, `koan_splash_logo.png`) and
`data/artwork/boot_splash/airos_splash_icons.png`. The script builds
`src/tools/generate_boot_screen.cpp` (CMake target `generate_boot_screen`,
not built by default) and needs `rsvg-convert` and `libpng-dev`. air/OS's own
`images-airos.h` also works:

    VOS_CMAKE_EXTRA_OPTS="-DVOS_SPLASH_IMAGES=$PWD/headers/private/kernel/boot/images-airos.h"

CMake reads the header from `VOS_SPLASH_IMAGES` and stops if it names
`images.h`, `images-tm*.h` or `images-sans-tm.h`: the Haiku logo images are
Haiku trademarks and must not ship. Set `VOS_SPLASH_TEST_DUMP=<prefix>` when
running `vos_splash_test` to get the rendered frame as `<prefix>-boot.ppm`
and `<prefix>-stages.ppm`.

## Stages

| N | Icon (`BOOT_SPLASH_STAGE_*`) | Linux boot point | Sent by |
| - | --- | --- | --- |
| 1 | `1_INIT_MODULES` | initramfs starts | `init-top/vos-splash` (starts the daemon) |
| 2 | `2_BOOTSTRAP_FS` | real root mounted | `init-bottom/vos-splash` |
| 3 | `3_INIT_DEVICES` | `sysinit.target` | `vos-splash-stage@3.service` |
| 4 | `4_MOUNT_BOOT_FS` | udev settled | `vos-splash-stage@4.service` |
| 5 | `5_INIT_CPU_MODULES` | `basic.target` | `vos-splash-stage@5.service` |
| 6 | `6_INIT_VM_MODULES` | registrar started | `vos-splash-stage@6.service` |
| 7 | `7_RUN_BOOT_SCRIPT` | app_server about to take the display | `app_server.service` `ExecStartPre` |

A stage lights every icon up to it, so a skipped stage leaves no gap.
The units and drop-ins are in `data/boot_splash/systemd/`; the drop-ins on
`sysinit.target`, `basic.target` and `registrar.service` pull the stages in,
so nothing needs `systemctl enable`. Only `splash` boots run any of it: the
Safe Mode, Recovery and SSH Debug entries keep the text console.

## Protocol

```
vos-splash --daemon [--lit N] [--handoff-timeout S]
vos-splash --stage N        # 1..7; 7 also hands off
vos-splash --deactivate     # drop DRM master, keep the picture
vos-splash --quit           # exit, leaving the picture on screen
```

The daemon reads lines from `/run/vos-splash.fifo` (`1`..`7`, `deactivate`,
`quit`), records the highest stage in `/run/vos-splash.stage` and writes
`/run/vos-splash.inactive` once it dropped master. Clients exit 0 when no
daemon serves the FIFO. `--daemon` returns once the splash is on screen.

## Surviving the switch to the real root

The daemon started in the initramfs keeps running on the real root, the way
plymouthd does: `run-init` does not kill it, and the initramfs `init` moves
`/dev` and `/run` (with the FIFO) onto the new root. After the switch the
path `/dev` of that process names the emptied initramfs, so the daemon opens
`/dev` and `/run` as directory fds at start and resolves everything through
them.

On Debian's kernel no GPU driver is in the initramfs: the splash first draws
on efifb (`/dev/fb0`). When udev on the real root loads the DRM driver, efifb
goes away; the daemon notices within 250 ms (the fbdev ioctls fail, or a
new `dri/card*` appears, or its DRM device answers `ENODEV`), opens the new
device and redraws with the stages already lit. Same for simpledrm being
replaced by the real driver.

If no daemon came from the initramfs (an initramfs built without the hook,
or no frame buffer before the GPU driver loaded), `vos-splash.service`
(pulled in by stage 3) starts one that redraws with at least stage 2 lit, or
the stage recorded in `/run/vos-splash.stage`. When the initramfs daemon is
alive it sees the FIFO already served and exits at once.

## Handoff to app_server

Stage 7 runs as `ExecStartPre` of `app_server.service` (with the greeter
enabled, of `janus.service`, which starts that app_server itself). The daemon
lights the last icon, calls `DRM_IOCTL_DROP_MASTER`, and writes
`/run/vos-splash.inactive`; `--stage 7` returns only then, so app_server
starts with the master free. The daemon keeps its fd open and the dumb
buffer on screen: no `lastclose`, so the kernel's fbdev emulation does not
put the text console back in between.

What app_server needs to do (it is not in this tree yet; nothing here
changes it):

1. Open the card and `drmSetMaster()` (or take it through libseat/logind) as
   it does now. This succeeds because the splash already dropped master.
2. After its first `drmModeSetCrtc()` (its own frame buffer on screen), run
   `/usr/libexec/vos-splash --quit`, or write `quit\n` to
   `/run/vos-splash.fifo`. The daemon then closes its fd; its frame buffer is
   no longer scanned out, so nothing flickers.

If app_server never says so, the daemon leaves by itself after the handoff
timeout (15 s). The VT stays in `KD_GRAPHICS` after a handoff; a `--quit`
without one (emergency/rescue, shutdown) puts it back in `KD_TEXT`.

## Building and testing

```
ninja vos-splash          # static, /usr/libexec/vos-splash
ninja check-vos-splash    # host tests, no display needed
```

`test/vos_splash_test.cpp` checks the stage-to-icon mapping, decodes the
air/OS images, and renders into memory at 32 and 16 bpp (logo centred and
not background, lit icons brighter than unlit ones, nothing written past a
too-small buffer).
