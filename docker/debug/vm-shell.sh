#!/bin/sh
# Turn a VM booted by qemu-headless.sh into one with a root shell on the
# serial console: catch the GRUB menu, append systemd.debug_shell=ttyS0 to
# the default "Vitruvian Live" entry and boot it. Run right after starting
# qemu-headless.sh; returns once the shell prompt answers.

set -eu

DBG_DIR=${VOS_DBG_DIR:-/tmp/vosdbg}
VMCTL="python3 -I $(dirname "$0")/vmctl.py"

i=0
until grep -q 'highlighted entry' "$DBG_DIR/serial.log" 2>/dev/null; do
	i=$((i + 1))
	[ "$i" -le 240 ] || { echo "error: GRUB menu not seen" >&2; exit 1; }
	sleep 0.5
done

$VMCTL send '\x1b[B\x1b[A' 1 >/dev/null   # any key stops the countdown
$VMCTL send 'e' 2 >/dev/null              # edit the highlighted entry
# Ctrl-N twice reaches the "linux" line, Ctrl-E its end.
$VMCTL send '\x0e\x0e\x05 systemd.debug_shell=ttyS0' 1 >/dev/null
$VMCTL send '\x18' 45 >/dev/null          # Ctrl-X boots

i=0
until $VMCTL send '\r' 2 | grep -q 'root@'; do
	i=$((i + 1))
	[ "$i" -le 30 ] || { echo "error: no root shell on serial" >&2; exit 1; }
done
echo "root shell ready on serial"
