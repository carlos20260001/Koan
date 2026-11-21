#!/bin/sh

# install ProcessController, NetworkStatus, PowerStatus & volume control in the Deskbar
# (on Linux the system lives in /system, and an applet that is not
# installed is skipped instead of failing the first login)
add_applet()
{
	[ -x "$1" ] || return 0
	"$@" || echo "default_deskbar_items: $1 failed" >&2
}

add_applet /system/apps/ProcessController -deskbar
add_applet /system/apps/NetworkStatus --deskbar
add_applet /system/apps/WiFiStatus
add_applet /system/apps/BluetoothStatus
add_applet /system/apps/PowerStatus --deskbar
add_applet /system/bin/desklink --add-volume

# install KeymapSwitcher for certain locales
case "${LC_ALL:-${LC_MESSAGES:-$LANG}}" in
	ru*|uk*|be*)
		add_applet /system/preferences/KeymapSwitcher --deskbar
		;;
esac

exit 0
