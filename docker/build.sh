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
#   VOS_WITH_WLROOTS ON to build the wlroots-koan Wayland server into
#                    app_server, OFF to leave it out. Unset: ON when
#                    VOS_CMAKE_EXTRA_OPTS has -DVOS_WITH_WLROOTS=ON, else what
#                    the build tree was configured with, else OFF (a tmpfs
#                    tree is always new, so it needs the variable). A tree
#                    configured with the other value is reconfigured.
#                    Inert in a tree without src/add-ons/wlroots-koan (the
#                    air/OS port has none yet): ignored with a note.
#   VOS_BUILDTOOLS_DIR  prebuilt host buildtools tree (with src/bin/rc/rc,
#                    src/bin/xres, src/bin/resattr), absolute or relative to
#                    VOS_SRC_DIR, so inside the bind mount. Skips step 1.
#                    Default: build them in $VOS_SRC_DIR/buildtools.
#
# This tree (air/OS with the VitruvianOS CMake build beside Jam) keeps
# VitruvianOS's configure as vos-configure; the root configure is Haiku's.
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

# A fresh tree (always the case on tmpfs) only gets the Wayland server when
# asked for, so resolve the request once and compare it with the tree.
case "${VOS_WITH_WLROOTS:-}" in
    ON|on|1|TRUE|true|YES|yes) WITH_WLROOTS=ON ;;
    OFF|off|0|FALSE|false|NO|no) WITH_WLROOTS=OFF ;;
    "")
        case " ${VOS_CMAKE_EXTRA_OPTS:-} " in
            *" -DVOS_WITH_WLROOTS=ON "*|*" -DVOS_WITH_WLROOTS=1 "*) WITH_WLROOTS=ON ;;
            *) WITH_WLROOTS="" ;;   # resolved against the build tree below
        esac
        ;;
    *) printf 'error: VOS_WITH_WLROOTS must be ON or OFF (got %s)\n' "$VOS_WITH_WLROOTS" >&2; exit 1 ;;
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
VOS_SRC_DIR, VOS_CCACHE, VOS_TMPFS_BUILD, VOS_WITH_WLROOTS,
VOS_BUILDTOOLS_DIR. See the header of this script for details.
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

# Prints why the buildtools are unavailable and how to get past it, then exits.
buildtools_unavailable() {
    printf '\nerror: the host buildtools (rc, xres, resattr) cannot be built from this tree yet.\n' >&2
    printf '%s\n' "$@" >&2
    cat >&2 <<'EOF'

The buildtools pass (cmake -DBUILDTOOLS_MODE=1) compiles src/build/libroot
and src/build/libbe, which take sources from src/system/libroot2 (milestone
M4 of PORTING.md) and the VitruvianOS kits. Until those land, either
  - put a prebuilt buildtools tree (from a VitruvianOS/Koan checkout or the
    vos-buildtools deb) inside the source tree and point at it:
        VOS_BUILDTOOLS_DIR=prebuilt-buildtools ./podman-compose up --build
    it needs src/bin/rc/rc, src/bin/xres and src/bin/resattr that run in
    this container; or
  - copy such a tree to ./buildtools, which is reused when its tools run.
See docs/koan/BUILDING.md.
EOF
    exit 1
}

_tools_run() {
    for _tool in src/bin/rc/rc src/bin/xres src/bin/resattr; do
        if [ ! -x "$1/$_tool" ] || ! "$1/$_tool" --help >/dev/null 2>&1; then
            return 1
        fi
    done
    return 0
}

if [ -n "${VOS_BUILDTOOLS_DIR:-}" ]; then
    # Escape hatch while the tree cannot build its own buildtools (M4).
    case "$VOS_BUILDTOOLS_DIR" in
        /*) BUILDTOOLS_PATH="$VOS_BUILDTOOLS_DIR" ;;
        *)  BUILDTOOLS_PATH="$SRC_DIR/$VOS_BUILDTOOLS_DIR" ;;
    esac
    _tools_run "$BUILDTOOLS_PATH" \
        || die "VOS_BUILDTOOLS_DIR=$VOS_BUILDTOOLS_DIR: $BUILDTOOLS_PATH needs src/bin/rc/rc, src/bin/xres and src/bin/resattr that run in this container (the path must be inside the bind-mounted source tree)"
    log "Using prebuilt buildtools: $BUILDTOOLS_PATH"
else
    BUILDTOOLS_PATH="$SRC_DIR/buildtools"

    # Buildtools are host binaries. Reuse them when they run in this
    # container; rebuild from scratch when they were produced against an
    # incompatible libc or for another architecture.
    if [ -f buildtools/build.ninja ] \
            && ! buildtools/src/bin/rc/rc --help >/dev/null 2>&1; then
        log "Existing buildtools cannot run here; rebuilding"
        rm -rf buildtools/CMakeCache.txt buildtools/CMakeFiles buildtools/build.ninja
    fi

    if ! _tools_run buildtools; then
        # air/OS port: libroot2 is milestone M4; without it the buildtools
        # configure stops on missing sources. Say so instead of letting
        # cmake print a page of errors.
        if [ ! -f src/system/libroot2/CMakeLists.txt ]; then
            buildtools_unavailable "src/system/libroot2 is not in this tree."
        fi
        log "Building host buildtools"
        mkdir -p buildtools
        _bt_log="$SRC_DIR/buildtools/buildtools.log"
        if ! ( cd buildtools && cmake -DBUILDTOOLS_MODE=1 .. -GNinja && ninja ) \
                >"$_bt_log" 2>&1; then
            tail -n 20 "$_bt_log" >&2
            _missing="$(awk '/Cannot find source file:/ { want = 1; next }
                want && NF { sub(/^ +/, "  "); print; want = 0 }' "$_bt_log" \
                | sort -u | head -n 10)"
            if [ -n "$_missing" ]; then
                buildtools_unavailable "Missing sources (full log: buildtools/buildtools.log):" "$_missing"
            fi
            die "building the buildtools failed; full log: buildtools/buildtools.log"
        fi
    fi
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
if [ -z "$WITH_WLROOTS" ]; then
    case "$(sed -n 's/^VOS_WITH_WLROOTS:BOOL=//p' CMakeCache.txt 2>/dev/null)" in
        ON|1|TRUE|YES) WITH_WLROOTS=ON ;;
        *) WITH_WLROOTS=OFF ;;
    esac
fi
if [ ! -f "$SRC_DIR/src/add-ons/wlroots-koan/CMakeLists.txt" ]; then
    # air/OS port: the Wayland server is not in this tree, so no CMake file
    # reads VOS_WITH_WLROOTS. Do not pass it on (CMake would warn about an
    # unused variable) and skip the reconfigure-on-change check below.
    if [ "$WITH_WLROOTS" = ON ]; then
        log "VOS_WITH_WLROOTS=ON ignored: src/add-ons/wlroots-koan is not in this tree"
    fi
    WITH_WLROOTS=OFF
    unset VOS_WITH_WLROOTS
elif [ "$WITH_WLROOTS" = ON ]; then
    export VOS_WITH_WLROOTS="$WITH_WLROOTS"
    log "Wayland server (wlroots-koan in app_server): ON"
else
    export VOS_WITH_WLROOTS="$WITH_WLROOTS"
    log "Wayland server (wlroots-koan in app_server): OFF -- app_server will not create a Wayland socket; set VOS_WITH_WLROOTS=ON to include it"
fi

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
    _cached_wlroots="$(sed -n 's/^VOS_WITH_WLROOTS:BOOL=//p' CMakeCache.txt 2>/dev/null)"
    case "$_cached_wlroots" in ON|1|TRUE|YES) _cached_wlroots=ON ;; *) _cached_wlroots=OFF ;; esac
    if [ "$_cached_wlroots" != "$WITH_WLROOTS" ]; then
        log "Wayland server setting changed: ${_cached_wlroots:-unset} -> $WITH_WLROOTS"
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
        export VOS_CMAKE_EXTRA_OPTS="${VOS_CMAKE_EXTRA_OPTS:-} -DCMAKE_C_COMPILER_LAUNCHER=ccache -DCMAKE_CXX_COMPILER_LAUNCHER=ccache"
    fi
    # When building on tmpfs, configure runs outside the source tree; tell it
    # where the source dir is so CMake doesn't resolve ".." to /tmp.
    set -- --arch="$ARCH" --build-type="$BUILD_TYPE" \
        --chroot-build --buildtools="$BUILDTOOLS_PATH"
    if [ "$SSHDEBUG" = 1 ]; then
        set -- "$@" --sshdebug
    fi
    # VitruvianOS's configure is vos-configure in this tree; the root
    # configure is Haiku's (Jam) and only forwards a fixed option list.
    if [ -n "$_TMPFS_MOUNT" ]; then
        VOS_CMAKE_SOURCE_DIR="$SRC_DIR" "$SRC_DIR/vos-configure" "$@"
    else
        ../vos-configure "$@"
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
