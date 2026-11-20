#!/bin/sh
# Copyright 2026, Koan contributors. All rights reserved.
# Distributed under the terms of the MIT License.
#
# Regenerates the Koan boot splash images header from its artwork:
#   data/artwork/boot_splash/koan_splash_logo.svg   (the Koan wordmark)
#   data/artwork/boot_splash/airos_splash_icons.png (air/OS's stage icons)
# into headers/private/kernel/boot/images-koan.h.
#
# Needs rsvg-convert (librsvg2-bin), python3 and libpng-dev, and a configured tree
# (BUILD_DIR, default generated.amd64). Run it from the top of the source
# tree; the build container has all but librsvg2-bin and libpng-dev.
set -eu

LOGO_WIDTH=${LOGO_WIDTH:-420}
ART=data/artwork/boot_splash
OUT=headers/private/kernel/boot/images-koan.h

LOGO_HEIGHT=${LOGO_HEIGHT:-120}
# vos-splash puts the icon row right under the logo image, so the gap
# between the wordmark and the icons is part of the logo (Haiku's and
# air/OS's logos have it too).
LOGO_BOTTOM_PADDING=${LOGO_BOTTOM_PADDING:-48}

# Wrap the wordmark, unchanged, in an SVG with the padding below it.
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
python3 - "$ART/koan_splash_logo.svg" "$TMP/padded.svg" \
	"$LOGO_WIDTH" "$LOGO_HEIGHT" "$LOGO_BOTTOM_PADDING" <<'PY'
import re, sys
src, dst, width, height, pad = sys.argv[1], sys.argv[2], *map(int, sys.argv[3:])
svg = open(src, encoding="utf-8").read()
svg = re.sub(r"<\?xml[^>]*>\s*", "", svg, count=1)
# The inner <svg> keeps its own viewBox and is drawn at width x height:
# drop its own width/height from the root tag and set ours.
start = svg.index("<svg")
end = svg.index(">", start)
tag = re.sub(r'\s(width|height)="[^"]*"', "", svg[start:end])
tag = tag.replace("<svg", f'<svg x="0" y="0" width="{width}" height="{height}"', 1)
svg = svg[:start] + tag + svg[end:]
total = height + pad
open(dst, "w", encoding="utf-8").write(
	f'<svg xmlns="http://www.w3.org/2000/svg" width="{width}" '
	f'height="{total}" viewBox="0 0 {width} {total}">{svg}</svg>\n')
PY

# The splash draws on black and generate_boot_screen strips alpha without
# blending it, so flatten the wordmark onto black here.
rsvg-convert --background-color black --format png \
	--output "$ART/koan_splash_logo.png" "$TMP/padded.svg"

# generate_boot_screen is an opt-in target of the CMake tree (src/tools).
BUILD_DIR=${BUILD_DIR:-generated.amd64}
[ -f "$BUILD_DIR/build.ninja" ] || {
	echo "error: configure the tree into $BUILD_DIR first (vos-configure)" >&2
	exit 1
}
ninja -C "$BUILD_DIR" generate_boot_screen
GEN="$BUILD_DIR/src/tools/generate_boot_screen"

# Logo and icon row centred like air/OS's (images-airos.h: 50/50, 50/50).
"$GEN" "$ART/koan_splash_logo.png" 50 50 \
	"$ART/airos_splash_icons.png" 50 50 "$OUT"
echo "wrote $OUT"
