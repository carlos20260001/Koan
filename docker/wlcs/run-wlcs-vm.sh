#!/bin/sh
# Run WLCS (github.com/canonical/wlcs) inside the debug VM against the
# live session: app_server's own Wayland socket, window manager and input.
#
#   docker/wlcs/run-wlcs-vm.sh [WLCS_SRC] [OUT_DIR] [-- gtest options...]
#
# WLCS_SRC  a WLCS checkout (default vendor/wlcs, tag v1.8.1)
# OUT_DIR   results (default generated.wlcs-vm/): wlcs.xml, wlcs.log,
#           summary.txt, plus incremental build trees
#
# Needs:
#   - the WLCS test image (see docker/wlcs/Containerfile);
#   - a VM from docker/debug/qemu-headless.sh with the serial root shell of
#     docker/debug/vm-shell.sh, running debs with koan_test_v1 (build them
#     with docker/debug/build-debs.sh, install with vm-install-debs.sh).
#
# What it does: builds wlcs and koan-wlcs-vm-integration.so; serves them to
# the guest; creates the root-owned /run/koan-test-control and restarts the
# session so app_server offers koan_test_v1; runs wlcs as the session user;
# uploads the results back; classifies them with classify.py.
#
# Environment: VOS_DBG_DIR (as for vmctl.py), WLCS_VM_TIMEOUT (seconds,
# default 5400), WLCS_VM_PORT (default 8766).
#
# The run leaves /run/koan-test-control in the guest: any session client
# can move windows and inject input until the VM is rebooted.

set -eu
SRC_DIR=$(cd "$(dirname "$0")/../.." && pwd)
WLCS_SRC=${1:-$SRC_DIR/vendor/wlcs}
OUT_DIR=${2:-$SRC_DIR/generated.wlcs-vm}
[ $# -ge 2 ] && shift 2 || shift $#
[ "${1:-}" = "--" ] && shift
WLCS_SRC=$(cd "$WLCS_SRC" && pwd)
mkdir -p "$OUT_DIR/stage"
OUT_DIR=$(cd "$OUT_DIR" && pwd)
PORT=${WLCS_VM_PORT:-8766}
TIMEOUT=${WLCS_VM_TIMEOUT:-5400}
VMCTL="python3 -I $SRC_DIR/docker/debug/vmctl.py"
TMP=$(mktemp -d)
trap 'kill $HTTP_PID 2>/dev/null; rm -rf "$TMP"' EXIT
HTTP_PID=

log() { printf '==> %s\n' "$*"; }
# The serial console echoes each command line, so a marker the guest
# prints is built with shell arithmetic (READY_$((40+2)) prints READY_42)
# and the echo of the command never matches it.

# ---- build --------------------------------------------------------------
cat > "$TMP/CMakeLists.txt" <<'CMAKE'
cmake_minimum_required(VERSION 3.25)
project(koan-wlcs C)
add_subdirectory("/vitruvian/src/add-ons/wlroots-koan" wlroots-koan)
CMAKE
log "building wlcs and koan-wlcs-vm-integration.so"
podman run --rm --entrypoint /bin/sh \
	-v "$SRC_DIR:/vitruvian:ro" -v "$WLCS_SRC:/wlcs:ro" \
	-v "$TMP:/wrapper:ro" -v "$OUT_DIR:/out" \
	localhost/koan-wlcs-test:latest -c '
	set -e
	cmake -S /wlcs -B /out/build-wlcs -G Ninja -DWLCS_BUILD_ASAN=OFF \
		-DWLCS_BUILD_TSAN=OFF -DWLCS_BUILD_UBSAN=OFF >/out/wlcs-cmake.log 2>&1
	ninja -C /out/build-wlcs wlcs >/out/wlcs-ninja.log
	cmake -S /wrapper -B /out/build-koan -G Ninja -DCMAKE_BUILD_TYPE=Debug \
		-DWLROOTS_KOAN_BUILD_TESTS=ON -DWLROOTS_KOAN_BUILD_WLCS=ON \
		-DWLCS_INCLUDE_DIR=/wlcs/include >/out/koan-cmake.log 2>&1
	ninja -C /out/build-koan koan-wlcs-vm-integration \
		koan-popup-follow-check >/out/koan-ninja.log
	cp /out/build-wlcs/wlcs \
		/out/build-koan/wlroots-koan/tests/koan-wlcs-vm-integration.so \
		/out/build-koan/wlroots-koan/tests/koan-popup-follow-check \
		/out/stage/'

# Runs "$@" as the session user with the session's environment (taken
# from Tracker, as the desktop starts apps).
cat > "$OUT_DIR/stage/asuser.sh" <<'SH'
#!/bin/sh
T=$(pgrep -x Tracker | head -n 1)
[ -n "$T" ] || { echo "asuser: no session (Tracker not running)" >&2; exit 1; }
U=$(awk '/^Uid:/ { print $2 }' /proc/$T/status)
G=$(awk '/^Gid:/ { print $2 }' /proc/$T/status)
ENV=$(tr '\0' '\n' < /proc/$T/environ \
	| grep -E '^(XDG_RUNTIME_DIR|WAYLAND_DISPLAY|HOME|USER|LOGNAME|LANG|PATH)=')
# shellcheck disable=SC2086
exec setpriv --reuid="$U" --regid="$G" --init-groups env -i $ENV "$@"
SH

# ---- serve the stage, accept the results --------------------------------
cat > "$TMP/serve.py" <<'PY'
import http.server, os, sys
stage, out, port = sys.argv[1], sys.argv[2], int(sys.argv[3])
class Handler(http.server.SimpleHTTPRequestHandler):
    def __init__(self, *a, **k):
        super().__init__(*a, directory=stage, **k)
    def do_PUT(self):
        name = os.path.basename(self.path)
        length = int(self.headers["Content-Length"])
        with open(os.path.join(out, name), "wb") as f:
            f.write(self.rfile.read(length))
        self.send_response(201)
        self.end_headers()
    def log_message(self, *a):
        pass
http.server.ThreadingHTTPServer(("127.0.0.1", port), Handler).serve_forever()
PY
python3 -I "$TMP/serve.py" "$OUT_DIR/stage" "$OUT_DIR" "$PORT" &
HTTP_PID=$!
sleep 1
URL=http://10.0.2.2:$PORT

# ---- guest: fetch, enable koan_test_v1, restart the session -------------
log "preparing the guest"
rm -f "$OUT_DIR/wlcs.xml" "$OUT_DIR/wlcs.log"
$VMCTL send "clear; rm -rf /tmp/wlcs; mkdir -p /tmp/wlcs && cd /tmp/wlcs && curl -fsS -O $URL/wlcs -O $URL/koan-wlcs-vm-integration.so -O $URL/asuser.sh -O $URL/koan-popup-follow-check && chmod 755 wlcs asuser.sh koan-popup-follow-check && chmod 1777 /tmp/wlcs; dpkg -s libwayland-server0 >/dev/null 2>&1 || apt-get install -y libwayland-server0 >/dev/null 2>&1; ldd ./wlcs | grep -c 'not found'; touch /run/koan-test-control; chown root:root /run/koan-test-control; echo GUEST_READY_\$((40+2)); cd /\r" 120 \
	| grep -q GUEST_READY_42 || { echo "error: guest preparation failed" >&2; exit 1; }

$VMCTL send "systemctl stop janus.service; loginctl terminate-user vos_login 2>/dev/null; loginctl terminate-user vos-live 2>/dev/null; sleep 2; systemctl start janus.service; sleep 8\r" 20 >/dev/null
$VMCTL hmp "sendkey ret" >/dev/null   # "Try It Now": start the live session
i=0
until $VMCTL send "clear; pgrep -x Tracker >/dev/null && grep -a -q 'koan_test_v1 test control is ENABLED' /var/log/app_server.log && echo SESSION_UP_\$((40+2))\r" 3 | grep -q SESSION_UP_42; do
	i=$((i + 1))
	[ "$i" -le 30 ] || { echo "error: session with koan_test_v1 did not come up" >&2; exit 1; }
	sleep 2
done
sleep 5  # let the desktop settle (Tracker, Deskbar windows)

# ---- run ----------------------------------------------------------------
log "running wlcs in the guest (timeout ${TIMEOUT}s)"
GTEST_ARGS=""
for a in "$@"; do GTEST_ARGS="$GTEST_ARGS '$a'"; done
$VMCTL send "clear; rm -f /tmp/wlcs/done; (cd /tmp/wlcs && timeout $TIMEOUT ./asuser.sh ./wlcs ./koan-wlcs-vm-integration.so --gtest_output=xml:/tmp/wlcs/wlcs.xml $GTEST_ARGS > /tmp/wlcs/wlcs.log 2>&1; echo \$? > /tmp/wlcs/done) &\r" 3 >/dev/null
start=$(date +%s)
until $VMCTL send "clear; test -f /tmp/wlcs/done && echo WLCS_DONE_\$(cat /tmp/wlcs/done)\r" 3 | grep -E -q 'WLCS_DONE_[0-9]'; do
	[ $(( $(date +%s) - start )) -le $((TIMEOUT + 120)) ] || { echo "error: wlcs did not finish" >&2; exit 1; }
	sleep 20
done

$VMCTL send "clear; cd /tmp/wlcs; curl -fsS -T wlcs.log $URL/wlcs.log; test -f wlcs.xml && curl -fsS -T wlcs.xml $URL/wlcs.xml; echo UPLOADED_\$((40+2)); cd /\r" 60 \
	| grep -q UPLOADED_42 || echo "warning: upload may have failed" >&2

grep -E "^\[  (PASSED|FAILED|SKIPPED) |tests ran|^\[==========\]" \
	"$OUT_DIR/wlcs.log" | tail -5 > "$OUT_DIR/summary.txt" || true
cat "$OUT_DIR/summary.txt"
[ -f "$OUT_DIR/wlcs.xml" ] || { echo "error: no wlcs.xml (see $OUT_DIR/wlcs.log)" >&2; exit 1; }
python3 -I "$SRC_DIR/docker/wlcs/classify.py" "$OUT_DIR/wlcs.xml" \
	--json "$OUT_DIR/classified.json" | tee "$OUT_DIR/classified.txt" | head -40
