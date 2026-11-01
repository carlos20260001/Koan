#!/bin/sh
# vitruvian-qemu-dev.sh — Automated VitruvianOS QEMU dev loop
#
# Usage:
#   ./vitruvian-qemu-dev.sh              # host build + ISO + QEMU boot (uses sudo if available)
#   ./vitruvian-qemu-dev.sh --build-only # host build + ISO, no boot
#   ./vitruvian-qemu-dev.sh --boot-only  # boot existing ISO, no build
#   ./vitruvian-qemu-dev.sh --rootless   # host build without sudo (proot/dpkg --root)
#   ./vitruvian-qemu-dev.sh --podman-compile   # rootless Podman compile only (no ISO, no boot)
#   ./vitruvian-qemu-dev.sh --podman-iso       # privileged Podman ISO build (no boot)
#   ./vitruvian-qemu-dev.sh --docker-iso       # privileged Docker ISO build (no boot)
#   ./vitruvian-qemu-dev.sh --console-stdout   # see serial output in your terminal
#
# Rootless mode:
#   - Detects automatically when running as non-root without sudo (VOS_ROOTLESS=1)
#   - Or force with --rootless
#   - Uses proot (if installed) or dpkg --root instead of mount/chroot/sudo
#   - Board images (raspberry, uboot, etc.) NOT supported rootless (need loop devices)
#   - ISO (raw/iso) supported rootless via mke2fs -d + mtools (no loop devices)
#
# Prerequisites (host mode):
#   - buildtools/ directory (from ./configure --build-buildtools)
#   - qemu-system-x86_64, ninja, cpack, grub-mkstandalone, xorriso, etc.
#   - sudo access (for full build) OR proot (for rootless build)
#
# Prerequisites (--podman-iso / --docker-iso):
#   - podman or docker installed
#   - Container run with --privileged (mount, chroot, losetup inside)
#   - ISO is written to the repo's output/ directory on the host
#
# Prerequisites (--podman-compile):
#   - podman installed, rootless capable
#   - Compile only; ISO + boot stay on host

set -e
set -o pipefail

BASEDIR="$(cd "$(dirname "$0")" && pwd)"
BUILD_TYPE="Debug"
ARCH="amd64"
IMAGE_TYPE="iso"
DO_BUILD=1
DO_BOOT=1
CONSOLE_STDOUT=0
FORCE_ROOTLESS=0
CONTAINER_MODE=""   # "" | podman-compile | podman-iso | docker-iso
CONTAINER_IMAGE="vitruvian-build"

for arg in "$@"; do
    case "$arg" in
        --build-only) DO_BOOT=0 ;;
        --boot-only)  DO_BUILD=0 ;;
        --console-stdout) CONSOLE_STDOUT=1 ;;
        --arch=*) ARCH="${arg#*=}" ;;
        --rootless) FORCE_ROOTLESS=1 ;;
        --podman-compile) CONTAINER_MODE="podman-compile" ;;
        --podman-iso)     CONTAINER_MODE="podman-iso" ;;
        --docker-iso)     CONTAINER_MODE="docker-iso" ;;
        --help|-h)
            echo "Usage: $0 [--build-only|--boot-only|--console-stdout|--arch=ARCH|--rootless|--podman-compile|--podman-iso|--docker-iso]"
            exit 0
            ;;
        *) echo "Unknown option: $arg" >&2; exit 1 ;;
    esac
done

cd "$BASEDIR"

# Export rootless flag so build scripts (common.sh, chroot.sh, image.sh) pick it up.
if [ "$FORCE_ROOTLESS" -eq 1 ]; then
    export VOS_ROOTLESS=1
    echo "==> Forced rootless mode (VOS_ROOTLESS=1)"
fi

container_run() {
    # $1 = engine (podman|docker), remaining args = command to run inside container
    _engine="$1"; shift
    case "$_engine" in
        podman)
            $_engine run --rm --privileged --network=host \
                --security-opt label=disable \
                -v "$BASEDIR:/vitruvian:z" \
                -w /vitruvian \
                "$CONTAINER_IMAGE" "$@"
            ;;
        docker)
            $_engine run --rm --privileged --network=host \
                -v "$BASEDIR:/vitruvian:/vitruvian" \
                -w /vitruvian \
                "$CONTAINER_IMAGE" "$@"
            ;;
    esac
}

build_container_image() {
    _engine="$1"
    if ! $_engine image exists "$CONTAINER_IMAGE" 2>/dev/null; then
        echo "==> Building $_engine image (debian:trixie + full build deps)..."
        $_engine build -t "$CONTAINER_IMAGE" - <<'EOF'
FROM debian:trixie
RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential cmake ninja-build debootstrap \
    grub-common grub-efi-amd64-bin grub-pc-bin \
    xorriso mtools dosfstools e2fsprogs \
    sudo curl ca-certificates git qemu-user-static proot \
    && rm -rf /var/lib/apt/lists/* \
    && rm -f /etc/sudoers.d/* \
    && echo 'root ALL=(ALL) NOPASSWD: ALL' > /etc/sudoers.d/vitruvian-root \
    && chmod 0440 /etc/sudoers.d/vitruvian-root
EOF
    fi
}

# --- Container ISO build modes ---
if [ "$CONTAINER_MODE" = "podman-iso" ] || [ "$CONTAINER_MODE" = "docker-iso" ]; then
    _engine="$(echo "$CONTAINER_MODE" | cut -d- -f1)"
    echo "==> $_engine privileged container ISO build (arch=$ARCH)..."
    echo "==> ISO will appear in output/ on the host."
    build_container_image "$_engine"

    echo "==> Configuring CMake inside $_engine..."
    container_run "$_engine" ./configure --build-buildtools
    container_run "$_engine" ./configure --arch="$ARCH" --build-type="$BUILD_TYPE" --chroot-build

    echo "==> Running ninja build inside $_engine..."
    container_run "$_engine" ninja

    echo "==> Building ISO inside $_engine..."
    container_run "$_engine" ./bake build --image-type="$IMAGE_TYPE" --arch="$ARCH"

    echo "==> ISO build complete. Artifacts in output/ on the host."
    echo "==> To boot: ./vitruvian-qemu-dev.sh --boot-only"
    exit 0
fi

# --- Rootless Podman compile-only ---
if [ "$CONTAINER_MODE" = "podman-compile" ]; then
    echo "==> Rootless Podman compile-only mode."
    echo "==> ISO building and QEMU boot are NOT done in Podman."
    echo "==>   (bake.sh needs sudo + mount + chroot + losetup; rootless"
    echo "==>    Podman cannot do those.)"

    _compile_image="vitruvian-compile"
    if ! podman image exists "$_compile_image" 2>/dev/null; then
        echo "==> Building Podman compile image (debian:trixie + build deps)..."
        podman build -t "$_compile_image" - <<'EOF'
FROM debian:trixie
RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential cmake ninja-build \
    && rm -rf /var/lib/apt/lists/*
EOF
    fi

    echo "==> Configuring CMake in Podman..."
    podman run --rm --network=host \
        --security-opt label=disable \
        --userns=host \
        -v "$BASEDIR:/vitruvian:z" \
        -w /vitruvian \
        "$_compile_image" ./configure --build-buildtools

    echo "==> Running ninja build in Podman..."
    podman run --rm --network=host \
        --security-opt label=disable \
        --userns=host \
        -v "$BASEDIR:/vitruvian:z" \
        -w /vitruvian \
        "$_compile_image" ninja 2>&1 | tee /tmp/vitruvian-ninja-podman.log

    echo "==> Compile done. ISO build + QEMU boot must run on host:"
    echo "    ./vitruvian-qemu-dev.sh --boot-only"
    exit 0
fi

# --- Host mode (default) ---

# --- 1. Ensure buildtools exists ---
if [ ! -d "$BASEDIR/buildtools" ]; then
    echo "==> buildtools/ not found. Bootstrapping host buildtools..."
    ./configure --build-buildtools
fi

# --- 2. Configure if not already configured ---
if [ ! -f "$BASEDIR/buildstate.conf" ]; then
    echo "==> Configuring build (arch=$ARCH, type=$BUILD_TYPE, chroot-build)..."
    ./configure --arch="$ARCH" --build-type="$BUILD_TYPE" --chroot-build
fi

# --- 3. Build ---
if [ "$DO_BUILD" -eq 1 ]; then
    echo "==> Running ninja build..."
    ninja -C "$BASEDIR" 2>&1 | tee /tmp/vitruvian-ninja.log

    echo "==> Building $IMAGE_TYPE image..."
    ./bake build --image-type="$IMAGE_TYPE" --arch="$ARCH" 2>&1 | tee /tmp/vitruvian-bake.log
fi

# --- 4. Boot in QEMU ---
if [ "$DO_BOOT" -eq 1 ]; then
    echo "==> Booting $IMAGE_TYPE in QEMU (arch=$ARCH)..."
    if [ "$CONSOLE_STDOUT" -eq 1 ]; then
        ./bake boot --image-type="$IMAGE_TYPE" --arch="$ARCH" \
            --enable-console-stdout
    else
        ./bake boot --image-type="$IMAGE_TYPE" --arch="$ARCH"
    fi
fi
