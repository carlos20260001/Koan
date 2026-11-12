# Fractional scaling: porting air/OS's render scale

Status: plan. Vitruvian's own fractional scaling and the input_server
scale code have been removed on `rework/scaling`. The previous
implementation is kept, unchanged, on the `scaling-input` branch (master at
2a2437b7).

## Why

air/OS (https://github.com/airos-works/haiku) has the same modernization
goals on a Haiku kernel and user land. Its scaling design
(`docs/airos/SCALING.md` there) keeps app_server, the Interface Kit and
input_server entirely in logical coordinates. Only rasterization works in
frame buffer pixels:

- `HWInterface::RenderScale()`: one desktop density, 100 to 250 percent in
  25 percent steps. Each logical coordinate is multiplied by it on its way
  into the frame buffer.
- Text is rendered from hinted glyph bitmaps at the exact fractional size,
  with grey anti-aliasing when scaled.
- Frames and separators are kept on the device pixel grid together
  (`FrameLines`, `HaikuControlLook`), so lines are the same width on every
  side.
- Bitmaps drawn one to one are area sampled; scaled bitmaps get a separable
  bilinear filter.
- Window drags copy whole frame buffer pixels and redraw a thin edge band.
- `BScreen::DeviceScale()` and `B_DEVICE_PIXEL_BITMAP` are available to
  programs that render their own pixels.

Our design converted pointer coordinates physical -> logical in
`EventDispatcher`, sent physical bounds to input_server, and scaled each
primitive in `DrawingEngine`/`Painter`. That split was the source of the
cursor clamping fixes (102dc40e, d465b9a7) and the repaint trails seen when
dragging windows at fractional scales. With air/OS's model input_server
needs no scale code at all.

## Rule: keep air/OS authorship

Every port lands as an air/OS commit with its original author as the git
author: `git cherry-pick -x <sha>` from an `airos` remote (or a merge commit
of it). Git records a single author, so the people who ported it are
credited as co-authors on the same commit:

```
Co-Authored-By: carlos20260001 <carlosforgithub2026@outlook.com>
Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
```

Changes needed for Linux, CMake and `DrmHWInterface` either go into the
ported commit while resolving it (still under the air/OS author, with the
trailers) or into separate commits that follow it. Never squash air/OS
code into commits that have only our authorship.

```sh
git remote add airos https://github.com/airos-works/haiku.git
git fetch airos master
git cherry-pick -x <sha>          # resolve conflicts, then:
git commit --amend --no-edit \
  --trailer "Co-Authored-By: carlos20260001 <carlosforgithub2026@outlook.com>" \
  --trailer "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

air/OS code is under the MIT License. Its notice is appended to the root
`LICENSE` and ships in the image as
`data/system/data/licenses/MIT (air-OS)`. Keep each ported file's
original copyright header; add a copyright holder there if a file gains
one.

The histories are unrelated, so expect conflicts. Haiku's `Jamfile` maps to
our `CMakeLists.txt`, and `AccelerantHWInterface` maps to
`DrmHWInterface`.

## Commits, in order

Core (needed for any scaling):

| air/OS | What |
|---|---|
| 87885422 | arrange and scale the monitors, keep windows on them (`DisplayLayout`, `SetRenderScale` protocol). 3bb2bbbf is the same change on another branch: take one only |
| 908c1fb3 | draw the screen at the monitors' density (`HWInterface`, `DrawingEngine`, `Painter`, cursor) |
| ee8d8390 | hardware cursor at the frame buffer's density (port to `DrmHWInterface` cursor plane) |
| 6a0056c5 | fractional drawing edges; vector icons at drawing density |
| 3528aa6e | keep adjacent displays together when changing their scale |
| 77d9b070 | draw at the largest display scale the hardware can show |
| 4843681f | `BScreen::DeviceScale()`, `B_DEVICE_PIXEL_BITMAP`; then point `BIconUtils::IconScale()` and the Wayland `wl_output` scale at it (both fixed at 1.0 now) |

Quality (after the core works in QEMU):

| air/OS | What |
|---|---|
| 722012ee, 68fa242f | scaled text from hinted glyph bitmaps; fractional font sizes measured as drawn |
| 448a83fc, 32cec2e0 | drag windows by copying pixels; no flash on inexact scroll copies |
| 30d7feae, 3232a571, d3799ca2 | bilinear filter and area sampling for bitmaps |
| fefba40e, 189f6f33, 6e588412, 5f046f17, 1062c0fb | frames, separators and window borders on the pixel grid |
| 2827a995 | translucent outlines on the pixel grid |

Our own follow-up: `DrmHWInterface::SetRenderScale()` (the frame buffer
in physical pixels, logical mode exposed to app_server), the Screen
preferences control and the Wayland output scale.

## What was removed on rework/scaling

- `ScreenScaleTransform.h`, `Painter/DeviceTransform.h`,
  `fractional-scale-v1-protocol.c`, `test_fractional_scaling`,
  `test_screen_scale_transform`, `docs/analysis/fractional-scaling.md`.
- `BScreen::GetScale()/SetScale()`, `AS_SCREEN_GET/SET_SCALE`,
  `Desktop::SetScreenScale()/PhysicalToLogical()/...`, per-screen
  `Screen::Scale()/LogicalFrame()`, the scale in `ScreenConfigurations`
  and `VirtualScreen`, the Painter/DrawingEngine/BitmapPainter scale
  paths, the scale slider in Screen preferences.
- input_server: `InputServer.cpp` and `MouseInputDevice.cpp` are back to
  upstream VitruvianOS.

Kept: the DRM cursor-plane changes, the evdev injected-event wake-up, the
`kEventStreamFence` test fence, the pointer-grab mouse state update, the
HiDPI icon/bitmap density API (at scale 1.0) and all Wayland work.
