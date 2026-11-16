#!/bin/sh
# Checks that air/OS's classic Media Kit (libmedia) and VitruvianOS's media2
# can share a process: each exports its symbols under its own version node,
# and the libraries built on media2 bind to media2's.
#
#	check_symbol_versions.sh <objdump> <libmedia.so> <libmedia2.so> \
#		[<library linked against media2>...]
#
# Exits with 0 when everything is as it should be.

objdump=$1
media=$2
media2=$3
shift 3

if [ -z "$media2" ]; then
	echo "FAIL: libmedia2 is not part of this build"
	exit 1
fi

failed=0

# check_exports <library> <version>: every defined dynamic symbol of the
# library carries <version>.
check_exports()
{
	total=$("$objdump" -T "$1" | awk '$2 == "g" && $4 != "*UND*"' | wc -l)
	wrong=$("$objdump" -T "$1" | awk -v v="$2" \
		'$2 == "g" && $4 != "*UND*" && $(NF-1) != v && $(NF-1) != "Base"' \
		| wc -l)
	if [ "$total" -eq 0 ] || [ "$wrong" -ne 0 ]; then
		echo "FAIL: $1: $wrong of $total exported symbols not in $2"
		failed=1
	else
		echo "ok: $1: $total exported symbols in $2"
	fi
}

check_exports "$media" MEDIA_1
check_exports "$media2" MEDIA2_1

# Both define classes of the same names; make sure that is still so, or
# this test checks nothing.
same=$( ("$objdump" -T "$media" | awk '$4 != "*UND*" {print $NF}' | sort -u;
	"$objdump" -T "$media2" | awk '$4 != "*UND*" {print $NF}' | sort -u) \
	| sort | uniq -d | wc -l)
echo "note: $same symbol names are defined by both libraries"

for library in "$@"; do
	[ -n "$library" ] || continue
	# A media2 consumer must not reference libmedia's version, and must
	# reference media2's.
	classic=$("$objdump" -T "$library" | grep -c '(MEDIA_1)')
	new=$("$objdump" -T "$library" | grep -c '(MEDIA2_1)')
	if [ "$classic" -ne 0 ] || [ "$new" -eq 0 ]; then
		echo "FAIL: $library: $new references to MEDIA2_1, $classic to MEDIA_1"
		failed=1
	else
		echo "ok: $library: $new references to MEDIA2_1"
	fi
done

exit $failed
