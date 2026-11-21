# Copyright 2026, Koan contributors. All rights reserved.
# Distributed under the terms of the MIT License.
#
# The Deskbar menu, as air/OS builds it (build/jam/packages/Haiku and the
# DESKBAR_* / SYSTEM_PREFERENCES / SYSTEM_DEMOS lists of
# build/jam/images/definitions/{minimum,regular}): eight folders in
# /system/data/deskbar/menu holding links to the programs, and the
# menu_entries virtual directory that merges them with the user's own
# menu folders. docs/airos/DESKBAR-MENU.md describes the result.
#
# Each entry is "<CMake target>" or "<CMake target>|<menu name>". Only
# entries whose target this tree builds get a link; a menu program that
# no other profile installs is installed here, where air/OS put it.
# Office, Media and Developer stay empty: air/OS fills them from packages
# built outside this tree.
#
# Folder names come from src/data/directory_attrs/deskbar-*.rdef (SYS:NAME
# attributes), which the Debian postinst stamps on the installed folders;
# menu_entries gets its application/x-vnd.vos-virtual-directory type there
# too, the type air/OS's Jamfile gave it.

set(_menu "/system/data/deskbar/menu")

set(AIROS_DESKBAR_APPLICATIONS
	# minimum
	DeskCalc
	Installer
	StyledEdit
	Terminal
	# regular
	HaikuDepot
	Icon-O-Matic
	Mail
	MediaConverter
	MidiPlayer
	People
	SoftwareUpdater
	SoundRecorder
	# air/OS: System Updater, named as people say it
	"SystemUpdater|System Updater"
)

set(AIROS_DESKBAR_UTILITIES
	CharacterMap
	Debugger
	Devices
	DiskProbe
	DiskUsage
	DriveSetup
	Expander
	CodyCam
	Magnify
	PoorMan
	SerialConnect
)

set(AIROS_DESKBAR_DESKTOP_APPLETS
	BluetoothStatus
	NetworkStatus
	WiFiStatus
	ProcessController
	Workspaces
	AutoRaise
	LaunchBox
	PowerStatus
)

# CMake names the Deskbar and Tracker preferences apart from the
# applications of the same name.
set(AIROS_DESKBAR_PREFERENCES
	Appearance
	Backgrounds
	"Deskbar_prefs|Deskbar"
	FileTypes
	Input
	Keymap
	Locale
	MCP
	Network
	WiFi
	Notifications
	Power
	Screen
	Shortcuts
	Time
	"Tracker_prefs|Tracker"
	VirtualMemory
	Bluetooth
	DataTranslations
	E-mail
	Media
	Printers
	Repositories
	ScreenSaver
	Sounds
)

set(AIROS_DESKBAR_DEMOS
	Chart
	Clock
	Cortex
	FontDemo
	GLTeapot
	Lookout
	Mandelbrot
	OverlayImage
	Pairs
	PatchBay
	Playground
	Pulse
	Sudoku
)

# What the other profiles already install, so nothing is installed twice.
set(_installed_apps ${CORE_APPLICATIONS} ${SYSTEM_APPS}
	${SYSTEM_APPS_UNLISTED} ${DESKBAR_DEMOS_TARGETS} ${DESKBAR_APPLETS})
set(_installed_preferences ${SYSTEM_PREFERENCES_TARGETS})

# DeskbarMenuFolder(<folder> <install dir> <list>)
function( DeskbarMenuFolder folder dir listvar )
	set(_dest "${_menu}/${folder}")
	ImageCreateDir("${_dest}")
	foreach(_entry IN LISTS ${listvar})
		string(REPLACE "|" ";" _parts "${_entry}")
		list(GET _parts 0 _target)
		list(LENGTH _parts _count)
		if(NOT TARGET ${_target})
			continue()
		endif()

		get_target_property(_file ${_target} OUTPUT_NAME)
		if(NOT _file)
			set(_file "${_target}")
		endif()
		if(_count GREATER 1)
			list(GET _parts 1 _name)
		else()
			set(_name "${_file}")
		endif()

		if(dir STREQUAL "/system/preferences")
			set(_known ${_installed_preferences})
		else()
			set(_known ${_installed_apps})
		endif()
		if(NOT _target IN_LIST _known)
			ImageInclude("${dir}" ${_target})
		endif()

		# relative, like air/OS's links: ../../../../apps/<name>
		file(RELATIVE_PATH _link "${_dest}" "${dir}/${_file}")
		install(CODE "
			file(CREATE_LINK \"${_link}\"
				\"\$ENV{DESTDIR}${_dest}/${_name}\" SYMBOLIC)
		" COMPONENT data)
	endforeach()
endfunction()

DeskbarMenuFolder("Applications" "/system/apps" AIROS_DESKBAR_APPLICATIONS)
ImageCreateDir("${_menu}/Office")
ImageCreateDir("${_menu}/Media")
ImageCreateDir("${_menu}/Developer")
DeskbarMenuFolder("Utilities" "/system/apps" AIROS_DESKBAR_UTILITIES)
DeskbarMenuFolder("Desktop applets" "/system/apps"
	AIROS_DESKBAR_DESKTOP_APPLETS)
DeskbarMenuFolder("Preferences" "/system/preferences"
	AIROS_DESKBAR_PREFERENCES)
# this tree installs the demos with the applications
DeskbarMenuFolder("Demos" "/system/apps" AIROS_DESKBAR_DEMOS)

ImageIncludeFile("src/data/deskbar/menu_entries" "/system/data/deskbar")
