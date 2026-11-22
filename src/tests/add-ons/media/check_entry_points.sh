#!/bin/sh
# Copyright 2026, Koan contributors.
# Distributed under the terms of the MIT License.
#
# Checks classic Media Kit add-ons as their loaders see them: each exports
# the entry point its loader looks up (instantiate_plugin for codec plugins,
# make_media_addon for node add-ons) and binds to libmedia's MEDIA_1 symbols,
# never to media2's MEDIA2_1 ones.
#
#	check_entry_points.sh <objdump> <entry point> <add-on>...
#
# Exits with 0 when every add-on passes.

objdump=$1
entry=$2
shift 2

status=0
for addon in "$@"; do
	failed=0
	symbols=$("$objdump" -T "$addon") || { echo "FAIL: $addon: objdump"; status=1; continue; }
	if ! echo "$symbols" | grep -E "[[:space:]]\.text[[:space:]].*[[:space:]]$entry\$" >/dev/null; then
		echo "FAIL: $addon does not export $entry"
		failed=1
	fi
	if echo "$symbols" | grep -w "MEDIA2_1" >/dev/null; then
		echo "FAIL: $addon binds to media2 (MEDIA2_1)"
		failed=1
	fi
	if ! echo "$symbols" | grep -w "MEDIA_1" >/dev/null; then
		echo "FAIL: $addon has no reference to libmedia (MEDIA_1)"
		failed=1
	fi
	if [ $failed -eq 0 ]; then
		echo "ok: $addon"
	else
		status=1
	fi
done
exit $status
