# Copyright 2026, Koan contributors. All rights reserved.
# Distributed under the terms of the MIT License.

# The shortcut_catcher filter's shared code (BitFieldTesters,
# CommandActuators, KeyInfos, ParseCommandLine: Jam's
# libshortcuts_shared.a) is compiled into libtracker, which exports it.
Application(
	Shortcuts

	SOURCES
	main.cpp
	MetaKeyStateMap.cpp
	ShortcutsApp.cpp
	ShortcutsWindow.cpp
	ShortcutsSpec.cpp
	PopUpColumn.cpp
	EditWindow.cpp

	LIBS tracker localestub
	RDEF Shortcuts.rdef
)

UsePrivateHeaders(Shortcuts interface)
target_include_directories(Shortcuts PRIVATE
	${PROJECT_SOURCE_DIR}/src/add-ons/input_server/filters/shortcut_catcher)
DoCatalogs("x-vnd.vos-Shortcuts" preferences/shortcuts)
