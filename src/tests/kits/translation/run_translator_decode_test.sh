#!/bin/sh
# Copyright 2026, Koan contributors.
# Distributed under the terms of the MIT License.
#
# Runs TranslatorDecodeTest against the translators just built. libroot
# needs the Nexus devices, so without them the test is skipped (77).
#
# Usage: run_translator_decode_test.sh <test program> <data directory>
#	<translator add-on>...

if [ ! -e /dev/nexus ] || [ ! -e /dev/nexus_area ] \
		|| [ ! -e /dev/nexus_node_monitor ]; then
	echo "SKIP: /dev/nexus* not present (nexus.ko not loaded)"
	exit 77
fi

program="$1"
data="$2"
shift 2

dir="$(mktemp -d)" || exit 1
trap 'rm -rf "$dir"' EXIT
for addon in "$@"; do
	ln -s "$addon" "$dir/$(basename "$addon")" || exit 1
done

"$program" "$dir" "$data"
