#!/bin/sh
# Incremental build of the .deb packages in an on-disk build tree, without
# assembling an ISO. Pairs with vm-install-debs.sh.
#
#   docker/debug/build-debs.sh [BUILD_DIR]
#
# BUILD_DIR defaults to generated.release. The tree must already be
# configured; the first time, run
#   VOS_WITH_WLROOTS=ON VOS_CMAKE_EXTRA_OPTS="-DVOS_WITH_WLROOTS=ON" \
#   VOS_BUILD_DIR=generated.release VOS_BUILD_TYPE=Release \
#   VOS_TMPFS_BUILD=0 VOS_IMAGE_TYPES=none ./podman-compose run -T --rm build ./docker/build.sh
# (VOS_IMAGE_TYPES=none stops after ninja and produces no debs; this script
# adds the cpack step bake runs before assembling images.)
#
# The debs land in BUILD_DIR/ (not BUILD_DIR/output/).

set -eu

SRC_DIR=$(cd "$(dirname "$0")/../.." && pwd)
BUILD_DIR=${1:-generated.release}

cd "$SRC_DIR"
[ -f "$BUILD_DIR/build.ninja" ] || {
	echo "error: $BUILD_DIR is not configured (no build.ninja); see the header" >&2
	exit 1
}

./podman-compose run -T --rm -w "/vitruvian/$BUILD_DIR" build sh -c '
	set -e
	ninja
	rm -f VOS-*-Linux-*.deb
	cpack
	ls -la VOS-*-Linux-*.deb'
