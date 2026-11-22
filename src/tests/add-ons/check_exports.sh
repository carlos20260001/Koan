#!/bin/sh
# Copyright 2026, Koan contributors.
# Distributed under the terms of the MIT License.
#
# Checks that add-ons export, as functions, the entry points their loader
# looks up with get_image_symbol().
#
#	check_exports.sh <objdump> <entry point>... -- <add-on>...
#
# Exits with 0 when every add-on exports every entry point.

objdump=$1
shift
entries=""
while [ $# -gt 0 ] && [ "$1" != "--" ]; do
	entries="$entries $1"
	shift
done
[ "$1" = "--" ] && shift
if [ -z "$entries" ] || [ $# -eq 0 ]; then
	echo "FAIL: usage: check_exports.sh <objdump> <entry>... -- <add-on>..."
	exit 1
fi

status=0
for addon in "$@"; do
	symbols=$("$objdump" -T "$addon") || { echo "FAIL: $addon: objdump"; status=1; continue; }
	failed=0
	for entry in $entries; do
		if ! echo "$symbols" | grep -E "[[:space:]]\.text[[:space:]].*[[:space:]]$entry\$" >/dev/null; then
			echo "FAIL: $addon does not export $entry"
			failed=1
		fi
	done
	if [ $failed -eq 0 ]; then
		echo "ok: $addon"
	else
		status=1
	fi
done
exit $status
