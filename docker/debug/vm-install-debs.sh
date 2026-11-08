#!/bin/sh
# Install freshly built debs into the running debug VM without rebuilding
# the ISO, then restart janus so the new servers are used.
#
#   docker/debug/vm-install-debs.sh [DEB_DIR]
#
# DEB_DIR defaults to generated.release (where build-debs.sh leaves them).
# Every VOS-*-Linux-*.deb there is offered; the guest installs the ones
# whose package it already has. The VOS packages depend on each other's
# exact version (and Break older ones), so they go in together with
# --auto-deconfigure; updating only some of them breaks apt in the guest.
#
# Needs the serial root shell from vm-shell.sh. The host serves DEB_DIR on
# 127.0.0.1:8765, which QEMU user networking exposes to the guest as
# 10.0.2.2:8765.

set -eu

SRC_DIR=$(cd "$(dirname "$0")/../.." && pwd)
DEB_DIR=${1:-$SRC_DIR/generated.release}
VMCTL="python3 -I $(dirname "$0")/vmctl.py"
PORT=8765

debs=$(cd "$DEB_DIR" && ls VOS-*-Linux-*.deb 2>/dev/null || true)
[ -n "$debs" ] || { echo "error: no VOS-*-Linux-*.deb in $DEB_DIR" >&2; exit 1; }

python3 -I -m http.server "$PORT" --bind 127.0.0.1 --directory "$DEB_DIR" \
	>/dev/null 2>&1 &
HTTP_PID=$!
trap 'kill $HTTP_PID 2>/dev/null' EXIT
sleep 1

fetch=""
for deb in $debs; do
	fetch="$fetch curl -fsS -O http://10.0.2.2:$PORT/$deb &&"
done

# Keep only debs whose package is installed in the guest.
select='sel=""; for d in *.deb; do dpkg -s "$(dpkg-deb -f $d Package)" >/dev/null 2>&1 && sel="$sel $d"; done; echo INSTALLING:$sel'

# Restart janus, then make sure no old session survives before starting
# it again: janus_session and its servers live in the user's logind
# session scope, not in janus.service's cgroup, so a janus that died
# without stopping its session (killed, crashed) leaves a second session
# holding DRM. Terminating the session users' logind sessions clears them.
restart='systemctl stop janus.service; loginctl terminate-user vos_login 2>/dev/null; loginctl terminate-user vos-live 2>/dev/null; sleep 2; systemctl start janus.service; sleep 8'

$VMCTL send "mkdir -p /tmp/debs && cd /tmp/debs && rm -f *.deb &&$fetch true; $select; dpkg -i --auto-deconfigure \$sel > /tmp/dpkg.log 2>&1; echo DPKG_EXIT=\$?; tail -3 /tmp/dpkg.log; md5sum /system/servers/app_server; $restart; pgrep -a app_server; cd /\r" 90
