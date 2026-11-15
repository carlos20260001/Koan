#!/bin/sh
# Boot a VitruvianOS ISO headless for agent-driven debugging.
#
#   docker/debug/qemu-headless.sh [ISO]
#
# Default ISO: generated.release/output/vos-uefi.iso. Runs in the
# foreground until the VM powers off (start it in the background).
#
# The display's preferred mode is VOS_VM_XRES x VOS_VM_YRES (default
# 1280x800); e.g. VOS_VM_XRES=2560 VOS_VM_YRES=1440 for a 2K screen.
#
# In $VOS_DBG_DIR (default /tmp/vosdbg; keep it short, UNIX socket paths
# must stay under 108 bytes):
#   ser.sock    interactive serial console (docker/debug/vmctl.py send)
#   serial.log  everything the serial console printed
#   qmp.sock    QMP monitor (vmctl.py shot / hmp)
#
# Differences from docker/run-qemu.sh: no display window, no audio, no
# host port forward, its own OVMF vars copy, so it can run next to a VM the
# user started with run-qemu.sh.

set -eu

SRC_DIR=$(cd "$(dirname "$0")/../.." && pwd)
ISO=${1:-$SRC_DIR/generated.release/output/vos-uefi.iso}
DBG_DIR=${VOS_DBG_DIR:-/tmp/vosdbg}
XRES=${VOS_VM_XRES:-1280}
YRES=${VOS_VM_YRES:-800}

[ -f "$ISO" ] || { echo "error: ISO not found: $ISO" >&2; exit 1; }
[ -f /usr/share/OVMF/OVMF_CODE_4M.fd ] || {
	echo "error: /usr/share/OVMF/OVMF_CODE_4M.fd not found (install ovmf)" >&2
	exit 1
}

mkdir -p "$DBG_DIR"
cp -f /usr/share/OVMF/OVMF_VARS_4M.fd "$DBG_DIR/vars.fd"
rm -f "$DBG_DIR/serial.log" "$DBG_DIR/ser.sock" "$DBG_DIR/qmp.sock"

exec qemu-system-x86_64 -m 4G -cpu host -smp 4 --enable-kvm \
	-drive if=pflash,format=raw,readonly=on,file=/usr/share/OVMF/OVMF_CODE_4M.fd \
	-drive if=pflash,format=raw,file="$DBG_DIR/vars.fd" \
	-cdrom "$ISO" -boot order=dc \
	-netdev user,id=n0 -device virtio-net-pci,netdev=n0 \
	-vga none -device virtio-vga,xres="$XRES",yres="$YRES",edid=on \
	-display none \
	-chardev socket,id=ser0,path="$DBG_DIR/ser.sock",server=on,wait=off,logfile="$DBG_DIR/serial.log" \
	-serial chardev:ser0 \
	-qmp unix:"$DBG_DIR/qmp.sock",server=on,wait=off
