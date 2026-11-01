#!/bin/sh
# In-container build pipeline for VitruvianOS.
#
# Runs inside the image from docker/Dockerfile with the source tree
# bind-mounted at /vitruvian. The steps are strictly sequential:
#
#   1. host buildtools (rc / xres / resattr)
#   2. configure the per-arch build tree + Debian chroot
#   3. bake (runs ninja, packages debs, assembles images)
#
# Keeping this in one process is what makes `podman-compose up` work: compose
# starts every service in parallel, so configure/ninja/bake as separate
# services race each other and ninja fails with "loading 'build.ninja'".
#
# Environment:
#   VOS_ARCH         amd64 | arm64 | arm32 | riscv64   (default: amd64)
#   VOS_BUILD_TYPE   Debug | Release                   (default: Debug)
#   VOS_IMAGE_TYPES  comma list: iso,raw,<board type>  (default: iso)
#                    "none" builds packages only
#   VOS_BUILD_DIR    build tree directory              (default: generated.$VOS_ARCH)
#   VOS_SRC_DIR      source tree inside the container  (default: /vitruvian)
#   VOS_CCACHE       1 to enable ccache (default: 1 if ccache binary exists)
#   VOS_TMPFS_BUILD  1 to build on a tmpfs in /tmp (default: auto when /tmp has space)
#   VOS_SSHDEBUG     1 to include the opt-in SSH debug boot and MCP server
#
# Usage:
#   docker/build.sh            # configure (if needed) + bake
#   docker/build.sh configure  # only create/refresh the build tree and chroot
#   docker/build.sh images     # only bake (configures first when the tree is missing)

set -eu

SRC_DIR="${VOS_SRC_DIR:-/vitruvian}"
ARCH="${VOS_ARCH:-amd64}"
BUILD_TYPE="${VOS_BUILD_TYPE:-Debug}"
IMAGE_TYPES="${VOS_IMAGE_TYPES:-iso}"
BUILD_DIR="${VOS_BUILD_DIR:-generated.$ARCH}"
SSHDEBUG="${VOS_SSHDEBUG:-0}"

case "$SSHDEBUG" in
    0|1) ;;
    *) printf 'error: VOS_SSHDEBUG must be 0 or 1 (got %s)\n' "$SSHDEBUG" >&2; exit 1 ;;
esac

log() { printf '\n==> %s\n' "$*"; }
die() { printf 'error: %s\n' "$*" >&2; exit 1; }

usage() {
    cat <<'EOF'
Usage: docker/build.sh [all|configure|images]

  all        Configure the build tree if needed, then bake (default)
  configure  Only create/refresh the build tree and chroot
  images     Bake (configures first when the tree is missing)

Environment: VOS_ARCH, VOS_BUILD_TYPE, VOS_IMAGE_TYPES, VOS_BUILD_DIR,
VOS_SRC_DIR, VOS_CCACHE, VOS_TMPFS_BUILD. See the header of this script for details.
EOF
}

step="${1:-all}"
case "$step" in
    all|configure|images) ;;
    -h|--help|help) usage; exit 0 ;;
    *) die "unknown step '$step' (use: all, configure, images)" ;;
esac

case "$IMAGE_TYPES" in
    none|packages) IMAGE_TYPES="" ;;
esac

cd "$SRC_DIR" || die "source tree not found: $SRC_DIR"
[ -f CMakeLists.txt ] || die "$SRC_DIR is not the Vitruvian source tree"

# A rootless container gets a privileged user namespace: mount/chroot/unshare
# work there, but losetup and mknod do not. Board images need both.
_uid0_host="$(awk '$1 == 0 { print $2; exit }' /proc/self/uid_map 2>/dev/null || true)"
if [ -n "$_uid0_host" ] && [ "$_uid0_host" != "0" ]; then
    for _t in $(printf '%s' "$IMAGE_TYPES" | tr ',' ' '); do
        case "$_t" in
            iso|raw) ;;
            *)
                die "image type '$_t' needs loop devices, which rootless podman cannot provide. Run the compose file with rootful podman (sudo ./podman-compose ...) or build that image on the host."
                ;;
        esac
    done
fi

# CMake invokes git in the source tree; declare it safe so a checkout owned
# by another uid (e.g. from a previous rootful run) does not break configure.
GIT_CONFIG_COUNT=1
GIT_CONFIG_KEY_0=safe.directory
GIT_CONFIG_VALUE_0="$SRC_DIR"
export GIT_CONFIG_COUNT GIT_CONFIG_KEY_0 GIT_CONFIG_VALUE_0

if [ ! -f src/system/kernel/nexus/CMakeLists.txt ]; then
    log "Initializing git submodules"
    git submodule update --init --recursive
fi

# Buildtools are host binaries. Reuse them when they run in this container;
# rebuild from scratch when they were produced against an incompatible libc
# or for another architecture.
if [ -f buildtools/build.ninja ] \
        && ! buildtools/src/bin/rc/rc --help >/dev/null 2>&1; then
    log "Existing buildtools cannot run here; rebuilding"
    rm -rf buildtools/CMakeCache.txt buildtools/CMakeFiles buildtools/build.ninja
fi

_need_buildtools=0
for _tool in src/bin/rc/rc src/bin/xres src/bin/resattr; do
    if [ ! -x "buildtools/$_tool" ] \
            || ! "buildtools/$_tool" --help >/dev/null 2>&1; then
        _need_buildtools=1
        break
    fi
done

if [ "$_need_buildtools" -eq 1 ]; then
    log "Building host buildtools"
    mkdir -p buildtools
    (
        cd buildtools
        cmake -DBUILDTOOLS_MODE=1 .. -GNinja
        ninja
    )
fi

# ---------------------------------------------------------------------------
# ccache setup — avoids recompiling unchanged sources (like cargo's build cache)
# ---------------------------------------------------------------------------
USE_CCACHE=0
if [ "${VOS_CCACHE:-auto}" != 0 ] && command -v ccache >/dev/null 2>&1; then
    USE_CCACHE=1
    CCACHE_DIR="${CCACHE_DIR:-/tmp/ccache}"
    mkdir -p "$CCACHE_DIR"
    export CCACHE_DIR
    export CCACHE_MAXSIZE="${CCACHE_MAXSIZE:-8G}"
    log "ccache enabled: dir=$CCACHE_DIR maxsize=$CCACHE_MAXSIZE"
fi

# ---------------------------------------------------------------------------
# Pre-flight: check available space before starting the build.
# A Debug tree + ISO needs ~20 GiB; Release + ISO needs ~8 GiB.
# ---------------------------------------------------------------------------
_needed_gib=8
case "$BUILD_TYPE" in
    Debug) _needed_gib=20 ;;
    Release) _needed_gib=8 ;;
    *) _needed_gib=10 ;;
esac

# If /tmp is a tmpfs with enough space, offer to build there instead of on
# the (bind-mounted, potentially slower disk) source tree.
_TMPFS_BUILD=0
if [ "${VOS_TMPFS_BUILD:-auto}" = "1" ] || { [ "${VOS_TMPFS_BUILD:-auto}" = "auto" ] && command -v mount >/dev/null 2>&1; }; then
    _tmp_avail="$(df -Pk /tmp 2>/dev/null | awk 'NR == 2 { printf "%d", $4 / 1048576 }')"
    if [ -n "$_tmp_avail" ] && [ "$_tmp_avail" -ge "$_needed_gib" ]; then
        _TMPFS_BUILD=1
        log "/tmp has ${_tmp_avail} GiB free (need ~${_needed_gib} GiB for $BUILD_TYPE); tmpfs build enabled."
    else
        log "/tmp has ${_tmp_avail:-unknown} GiB free (need ~${_needed_gib} GiB); building on source tree disk."
    fi
fi

# Determine the build directory. If tmpfs build is enabled, build under /tmp
# and symlink artifacts back into the source tree.
_TMPFS_MOUNT=""
if [ "$_TMPFS_BUILD" -eq 1 ]; then
    _TMPFS_MOUNT="/tmp/vos-build-${ARCH}-${BUILD_TYPE}"
    mkdir -p "$_TMPFS_MOUNT"
    mount -t tmpfs -o size="${_needed_gib}g" tmpfs "$_TMPFS_MOUNT" 2>/dev/null \
        && log "Mounted tmpfs at $_TMPFS_MOUNT" \
        || { log "tmpfs mount failed, falling back to source tree"; _TMPFS_BUILD=0; _TMPFS_MOUNT=""; }
fi

if [ -n "$_TMPFS_MOUNT" ]; then
    ACTUAL_BUILD_DIR="$_TMPFS_MOUNT"
    log "Building in tmpfs: $ACTUAL_BUILD_DIR (artifacts will be synced to $BUILD_DIR/output)"
else
    ACTUAL_BUILD_DIR="$BUILD_DIR"
    log "Building on disk: $ACTUAL_BUILD_DIR"
fi

mkdir -p "$ACTUAL_BUILD_DIR"
cd "$ACTUAL_BUILD_DIR"

# ---------------------------------------------------------------------------
# Configure the per-arch build tree + Debian chroot.
# ---------------------------------------------------------------------------
_force_configure=0
if [ -f build.ninja ] && [ -f buildconfig.conf ]; then
    _cached_type="$(sed -n 's/^CMAKE_BUILD_TYPE=//p' buildconfig.conf)"
    if [ -n "$_cached_type" ] && [ "$_cached_type" != "$BUILD_TYPE" ]; then
        log "Build type changed: $_cached_type -> $BUILD_TYPE"
        _force_configure=1
    fi
    _cached_sshdebug="$(sed -n 's/^VOS_SSHDEBUG=//p' buildconfig.conf)"
    if [ "$_cached_sshdebug" != "$SSHDEBUG" ]; then
        log "SSH-debug setting changed: ${_cached_sshdebug:-unset} -> $SSHDEBUG"
        _force_configure=1
    fi
fi

if [ ! -f build.ninja ] || [ ! -f CMakeCache.txt ] \
        || [ ! -d image_tree/chroot ] || [ "$_force_configure" -eq 1 ]; then
    log "Configuring $ARCH ($BUILD_TYPE) in $ACTUAL_BUILD_DIR"
    # ccache works through CMAKE_COMPILER_LAUNCHER; CMake picks it up automatically.
    # Do NOT set CC/CXX to "ccache gcc" — that breaks compiler detection
    # (ccache -dumpmachine without a real compiler wrapped fails).
    if [ "$USE_CCACHE" -eq 1 ]; then
        export VOS_CMAKE_EXTRA_OPTS="-DCMAKE_C_COMPILER_LAUNCHER=ccache -DCMAKE_CXX_COMPILER_LAUNCHER=ccache"
    fi
    # When building on tmpfs, configure runs outside the source tree; tell it
    # where the source dir is so CMake doesn't resolve ".." to /tmp.
    set -- --arch="$ARCH" --build-type="$BUILD_TYPE" \
        --chroot-build --buildtools="$SRC_DIR/buildtools"
    if [ "$SSHDEBUG" = 1 ]; then
        set -- "$@" --sshdebug
    fi
    if [ -n "$_TMPFS_MOUNT" ]; then
        VOS_CMAKE_SOURCE_DIR="$SRC_DIR" "$SRC_DIR/configure" "$@"
    else
        ../configure "$@"
    fi
else
    log "Build tree already configured: $ACTUAL_BUILD_DIR"
fi

if [ "$step" = "configure" ]; then
    log "Configure step complete"
    exit 0
fi

# ---------------------------------------------------------------------------
# Build (ninja + cpack + image assembly)
# ---------------------------------------------------------------------------
if [ -z "$IMAGE_TYPES" ]; then
    log "Building packages (no images)"
    ninja
else
    log "Building images: $IMAGE_TYPES"
    # bake runs ninja internally; pass ccache env so any direct compiler calls
    # also hit the cache.
    if [ "$USE_CCACHE" -eq 1 ]; then
        export CC="gcc"
        export CXX="g++"
    fi
    if [ -n "$_TMPFS_MOUNT" ]; then
        "$SRC_DIR/bake" build --image-type="$IMAGE_TYPES"
    else
        ../bake build --image-type="$IMAGE_TYPES"
    fi
fi

# ---------------------------------------------------------------------------
# Sync artifacts back to the source tree when building on tmpfs.
# ---------------------------------------------------------------------------
if [ -n "$_TMPFS_MOUNT" ] && [ -d output ]; then
    log "Syncing artifacts to source tree: $SRC_DIR/$BUILD_DIR/output"
    mkdir -p "$SRC_DIR/$BUILD_DIR/output"
    cp -rf output/* "$SRC_DIR/$BUILD_DIR/output/" 2>/dev/null || true
fi

log "Artifacts: $ACTUAL_BUILD_DIR/output"
ls -lh output 2>/dev/null || true

# ---------------------------------------------------------------------------
# Cleanup: unmount tmpfs if we mounted it.
# ---------------------------------------------------------------------------
if [ -n "$_TMPFS_MOUNT" ]; then
    umount "$_TMPFS_MOUNT" 2>/dev/null || true
    rmdir "$_TMPFS_MOUNT" 2>/dev/null || true
    log "tmpfs unmounted: $_TMPFS_MOUNT"
fi
