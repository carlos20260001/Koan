#!/bin/sh
# Run WLCS (github.com/canonical/wlcs) against the Koan Wayland server.
#
#   docker/wlcs/run-wlcs.sh [WLCS_SRC] [OUT_DIR] [-- gtest options...]
#
# WLCS_SRC  a WLCS checkout (default vendor/wlcs, tag v1.8.1)
# OUT_DIR   where results go (default generated.wlcs/); gets
#           wlcs.xml (gtest XML), wlcs.log and summary.txt
#
# Needs the test image (once):
#   podman build -t localhost/koan-wlcs-test:latest -f docker/wlcs/Containerfile docker/wlcs
#
# Builds WLCS and koan-wlcs-integration.so in the container (no sanitizer
# variants), then runs the whole suite. Exit status is WLCS's.

set -eu
SRC_DIR=$(cd "$(dirname "$0")/../.." && pwd)
WLCS_SRC=${1:-$SRC_DIR/vendor/wlcs}
OUT_DIR=${2:-$SRC_DIR/generated.wlcs}
[ $# -ge 2 ] && shift 2 || shift $#
[ "${1:-}" = "--" ] && shift
WLCS_SRC=$(cd "$WLCS_SRC" && pwd)
mkdir -p "$OUT_DIR"
OUT_DIR=$(cd "$OUT_DIR" && pwd)
WRAPPER=$(mktemp -d)
trap 'rm -rf "$WRAPPER"' EXIT
cat > "$WRAPPER/CMakeLists.txt" <<'CMAKE'
cmake_minimum_required(VERSION 3.25)
project(koan-wlcs C)
add_subdirectory("/vitruvian/src/add-ons/wlroots-koan" wlroots-koan)
CMAKE

podman run --rm --entrypoint /bin/sh \
	-v "$SRC_DIR:/vitruvian:ro" -v "$WLCS_SRC:/wlcs:ro" \
	-v "$WRAPPER:/wrapper:ro" -v "$OUT_DIR:/out" \
	localhost/koan-wlcs-test:latest -c '
	set -e
	# Build trees live in OUT_DIR so reruns are incremental.
	cmake -S /wlcs -B /out/build-wlcs -G Ninja -DWLCS_BUILD_ASAN=OFF \
		-DWLCS_BUILD_TSAN=OFF -DWLCS_BUILD_UBSAN=OFF >/out/wlcs-cmake.log
	ninja -C /out/build-wlcs wlcs >/out/wlcs-ninja.log
	cmake -S /wrapper -B /out/build-koan -G Ninja -DCMAKE_BUILD_TYPE=Debug \
		-DWLROOTS_KOAN_BUILD_TESTS=ON -DWLROOTS_KOAN_BUILD_WLCS=ON \
		-DWLCS_INCLUDE_DIR=/wlcs/include >/out/koan-cmake.log
	ninja -C /out/build-koan koan-wlcs-integration >/out/koan-ninja.log
	export XDG_RUNTIME_DIR=$(mktemp -d)
	set +e
	timeout 1800 /out/build-wlcs/wlcs \
		/out/build-koan/wlroots-koan/tests/koan-wlcs-integration.so \
		--gtest_output=xml:/out/wlcs.xml "$@" > /out/wlcs.log 2>&1
	status=$?
	grep -E "^\[  (PASSED|FAILED|SKIPPED) |tests ran|^\[==========\]" /out/wlcs.log \
		| tail -5 > /out/summary.txt
	exit $status' sh "$@"
