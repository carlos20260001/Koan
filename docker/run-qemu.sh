#!/bin/sh
# docker/run-qemu.sh — Boot a VitruvianOS image built by podman in QEMU.
#
# QEMU runs on the HOST (needs KVM + OVMF). The image is whatever podman
# baked into generated.<arch>/output/.
#
# Usage:
#   ./docker/run-qemu.sh                          # boot latest iso
#   ./docker/run-qemu.sh --build-dir=generated.release
#   ./docker/run-qemu.sh --image-type=raw
#   ./docker/run-qemu.sh --console-stdout         # serial to terminal
#   ./docker/run-qemu.sh --arch=amd64
#   ./docker/run-qemu.sh --bios                   # SeaBIOS instead of UEFI
#
# Examples:
#   VOS_BUILD_DIR=generated.release VOS_BUILD_TYPE=Release \
#       ./podman-compose up --build
#   ./docker/run-qemu.sh --build-dir=generated.release --console-stdout
#
# Prerequisites (host): qemu-system-x86_64, ovmf, /dev/kvm access

set -eu

BASEDIR="$(cd "$(dirname "$0")/.." && pwd)"
cd "$BASEDIR"

BUILD_DIR=""
IMAGE_TYPE="iso"
CONSOLE_STDOUT=0
ARCH="amd64"
FIRMWARE="uefi"
SHARED="$BASEDIR/shared"

for arg in "$@"; do
    case "$arg" in
        --build-dir=*)   BUILD_DIR="${arg#*=}" ;;
        --image-type=*)  IMAGE_TYPE="${arg#*=}" ;;
        --console-stdout) CONSOLE_STDOUT=1 ;;
        --arch=*)        ARCH="${arg#*=}" ;;
        --bios)          FIRMWARE="bios" ;;
        --shared-folder=*) SHARED="${arg#*=}" ;;
        --help|-h)
            echo "Usage: $0 [--build-dir=DIR] [--image-type=iso|raw] [--console-stdout] [--arch=amd64] [--bios] [--shared-folder=DIR]"
            exit 0
            ;;
        *) echo "Unknown option: $arg" >&2; exit 1 ;;
    esac
done

# Auto-detect build dir: prefer a Release tree if present, else Debug.
if [ -z "$BUILD_DIR" ]; then
    if [ -d generated.release/output ]; then
        BUILD_DIR=generated.release
    elif [ -d generated.amd64/output ]; then
        BUILD_DIR=generated.amd64
    else
        echo "error: no generated.<arch>/output/ found. Build an image first:" >&2
        echo "  ./podman-compose up --build" >&2
        exit 1
    fi
fi

IMG_DIR="$BUILD_DIR/output"
case "$IMAGE_TYPE" in
    iso)   IMG="$IMG_DIR/vos-uefi.iso" ;;
    raw)   IMG="$IMG_DIR/vos-uefi.raw" ;;
    *)     echo "error: unsupported image type '$IMAGE_TYPE' (use iso or raw)" >&2; exit 1 ;;
esac

[ -f "$IMG" ] || {
    echo "error: image not found: $IMG" >&2
    echo "Build it first:" >&2
    echo "  VOS_IMAGE_TYPES=$IMAGE_TYPE VOS_BUILD_DIR=$BUILD_DIR ./podman-compose up --build" >&2
    exit 1
}

# Host-side checks.
case "$ARCH" in
    amd64)
        QEMU=qemu-system-x86_64
        ;;
    arm64)
        QEMU=qemu-system-aarch64
        ;;
    riscv64)
        QEMU=qemu-system-riscv64
        ;;
    *)
        echo "error: arch '$ARCH' not supported by this script (amd64/arm64/riscv64)" >&2
        exit 1
        ;;
esac

command -v "$QEMU" >/dev/null 2>&1 || {
    echo "error: $QEMU not found on host. Install qemu-system-$ARCH." >&2
    exit 1
}

# Shared folder for host <-> guest file exchange (virtio-9p).
mkdir -p "$SHARED"

# OVMF vars file (per image type, so parallel boots don't clash).
VARS_FD="$BASEDIR/OVMF_VARS.$IMAGE_TYPE.fd"

LOG="$BASEDIR/vitruvian-console.log"

echo "==> Booting $IMG ($ARCH, $IMAGE_TYPE, $FIRMWARE) in QEMU..."
echo "==> Build dir: $BUILD_DIR"
echo "==> Shared folder: $SHARED"
[ "$CONSOLE_STDOUT" -eq 1 ] && echo "==> Serial: stdout" || echo "==> Serial log: $LOG"

case "$ARCH" in
    amd64)
        if [ "$FIRMWARE" = "bios" ]; then
            FW_ARGS=""
        else
            [ -f /usr/share/OVMF/OVMF_CODE_4M.fd ] || {
                echo "error: /usr/share/OVMF/OVMF_CODE_4M.fd not found. Install package 'ovmf'." >&2
                exit 1
            }
            cp -f /usr/share/OVMF/OVMF_VARS_4M.fd "$VARS_FD"
            FW_ARGS="-drive if=pflash,format=raw,readonly=on,file=/usr/share/OVMF/OVMF_CODE_4M.fd \
                     -drive if=pflash,format=raw,file=$VARS_FD"
        fi

        if [ "$CONSOLE_STDOUT" -eq 1 ]; then
            SERIAL="-serial mon:stdio"
        else
            SERIAL="-serial file:$LOG"
        fi

        # shellcheck disable=SC2086
        "$QEMU" \
            -m 8G -cpu host -smp "sockets=1,cores=2,threads=2" --enable-kvm \
            $FW_ARGS \
            -cdrom "$IMG" -boot order=dc,menu=on \
            -netdev user,id=mynet,hostfwd=tcp::2222-:22 \
            -device virtio-net-pci,netdev=mynet \
            -device intel-hda -device hda-duplex,audiodev=snd0 -audiodev pa,id=snd0 \
            -vga none -device virtio-vga,xres=1280,yres=800,edid=on \
            -virtfs local,path="$SHARED",mount_tag=host_shared,security_model=mapped-xattr,id=host_shared \
            $SERIAL
        ;;

    arm64)
        if [ "$CONSOLE_STDOUT" -eq 1 ]; then
            SERIAL="-serial mon:stdio"
        else
            SERIAL="-serial file:$LOG"
        fi

        # shellcheck disable=SC2086
        "$QEMU" \
            -m 4G -smp 2 -machine virt -cpu cortex-a72 \
            -bios /usr/share/qemu-efi-aarch64/QEMU_EFI.fd \
            -cdrom "$IMG" -boot order=dc,menu=on \
            -netdev user,id=mynet,hostfwd=tcp::2222-:22 \
            -device virtio-net-pci,netdev=mynet \
            -virtfs local,path="$SHARED",mount_tag=host_shared,security_model=mapped-xattr,id=host_shared \
            $SERIAL
        ;;

    riscv64)
        if [ "$CONSOLE_STDOUT" -eq 1 ]; then
            SERIAL="-serial mon:stdio"
        else
            SERIAL="-serial file:$LOG"
        fi

        # shellcheck disable=SC2086
        "$QEMU" \
            -m 4G -smp 2 -machine virt \
            -cdrom "$IMG" -boot order=dc,menu=on \
            -netdev user,id=mynet,hostfwd=tcp::2222-:22 \
            -device virtio-net-pci,netdev=mynet \
            -virtfs local,path="$SHARED",mount_tag=host_shared,security_model=mapped-xattr,id=host_shared \
            $SERIAL
        ;;
esac
