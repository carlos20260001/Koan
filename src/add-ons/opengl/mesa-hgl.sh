#!/bin/sh
# Copyright 2026, Koan contributors. All rights reserved.
# Distributed under the terms of the MIT License.
#
# Build air/OS's OpenGL route for the BeAPI on Linux: Mesa (3rdparty/mesa)
# with air/OS's Zink renderer series and Koan's Linux-host series applied,
# configured for the Haiku platform against this tree's headers and libbe.
# GLView.h and GLRenderer.h come from Mesa's own include/HaikuGL.
# Called by src/add-ons/opengl/CMakeLists.txt; see PORTING.md, M10 - air/OS
# Mesa (hgl + Zink).
#
# usage: mesa-hgl.sh <mesa source> <work dir> <stage dir> <sdk args>
#                    <sdk link args> <patch dir>...
#
# <sdk args> and <sdk link args> are comma separated (meson array options).
# The source is copied into <work dir>/src and patched there, so the
# submodule stays clean; the copy is redone whenever the source version or
# a patch changes. The result is installed into <stage dir>.
set -eu

src=$1
work=$2
stage=$3
sdk_args=$4
sdk_link_args=$5
shift 5

copy="$work/src"
build="$work/build"
stamp="$work/source.stamp"

# What the patched copy was made from: the Mesa version and every patch.
want=$( (cat "$src/VERSION"; for dir in "$@"; do
	for patch in "$dir"/*.patch; do
		[ -f "$patch" ] && cat "$patch"
	done
done) | sha256sum | cut -d' ' -f1)

if [ ! -f "$stamp" ] || [ "$(cat "$stamp")" != "$want" ]; then
	echo "mesa-hgl: preparing the patched source in $copy"
	rm -rf "$copy" "$build" "$stamp"
	mkdir -p "$copy"
	# No git here: the build container does not see the worktree's .git.
	tar -C "$src" --exclude=.git -cf - . | tar -C "$copy" -xf -
	for dir in "$@"; do
		for patch in "$dir"/*.patch; do
			[ -f "$patch" ] || continue
			echo "mesa-hgl: applying $(basename "$patch")"
			patch -d "$copy" -p1 -s -N < "$patch"
		done
	done
	echo "$want" > "$stamp"
fi

# A different SDK (include or link arguments) needs a new meson setup.
config_stamp="$work/config.stamp"
config=$(printf '%s\n%s\n' "$sdk_args" "$sdk_link_args" | sha256sum | cut -d' ' -f1)
if [ -f "$build/build.ninja" ] && { [ ! -f "$config_stamp" ] ||
		[ "$(cat "$config_stamp")" != "$config" ]; }; then
	echo "mesa-hgl: the SDK arguments changed, configuring again"
	rm -rf "$build"
fi

if [ ! -f "$build/build.ninja" ]; then
	# Zink renders through the Vulkan loader; softpipe (swrast) is the
	# fallback inside the same add-on (HGL_NO_ZINK, or no Vulkan device).
	# No GLX, EGL or GBM: Debian's glvnd libraries stay the GL of X11 and
	# Wayland clients.
	meson setup "$build" "$copy" \
		--prefix=/usr --libdir=lib \
		--buildtype=release -Db_ndebug=true \
		--wrap-mode=nodownload \
		-Dplatforms=haiku \
		-Dgallium-drivers=zink,swrast \
		-Dvulkan-drivers= -Ddri-drivers= \
		-Dglx=disabled -Degl=disabled -Dgbm=disabled \
		-Dgles1=disabled -Dgles2=disabled -Dosmesa=false \
		-Dllvm=disabled -Dshared-glapi=enabled -Ddri3=disabled \
		-Dlibunwind=disabled -Dvalgrind=disabled \
		-Dhaiku-lib-suffix=-hgl \
		"-Dhaiku-sdk-args=$sdk_args" \
		"-Dhaiku-sdk-link-args=$sdk_link_args"
	echo "$config" > "$config_stamp"
fi

ninja -C "$build"
rm -rf "$stage"
DESTDIR="$stage" meson install -C "$build" --no-rebuild --quiet
