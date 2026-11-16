# Boot splash (port pending, not built)

These are the original Haiku boot splash sources, kept here as the starting
point for milestone M7 in `PORTING.md`: port them to Linux so this splash
replaces Plymouth.

- `video_splash.cpp` - boot loader side: decompresses the splash images and
  draws the logo and the empty icon row (was
  `src/system/boot/platform/generic/video_splash.cpp`).
- `boot_splash.cpp` - kernel side: lights up one icon per boot stage (was
  `src/system/kernel/boot_splash.cpp`, deleted with the Haiku kernel in
  `564cc23cd0`).
- Headers stay in `headers/private/kernel/`: `boot_splash.h`,
  `boot/platform/generic/video_splash.h`, `video_blitter.h`, and the image
  headers produced by `src/tools/generate_boot_screen.cpp`.

Artwork: only the air/OS images (`data/artwork/boot_splash/airos_splash_*.png`,
`headers/private/kernel/boot/images-airos.h`) may be used. The Haiku logo
images (`splash_logo*`, `images-tm*.h`) are Haiku trademarks and must not
ship.
