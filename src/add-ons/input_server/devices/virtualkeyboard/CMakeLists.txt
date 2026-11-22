# Copyright 2026, Koan contributors.
# Distributed under the terms of the MIT License.

# On-screen keyboard. The keyboard layout and keymap code is the Keymap
# preference's, built with VIRTUAL_KEYBOARD_DEVICE as in the Jamfile this
# replaces.
set(_keymap_dir ${CMAKE_SOURCE_DIR}/src/preferences/keymap)
AddOn(
	virtualkeyboard
	SHARED

	LIBS
	be tracker shared localestub

	SOURCES
	VirtualKeyboardInputDevice.cpp
	VirtualKeyboardWindow.cpp
	${_keymap_dir}/KeyboardLayout.cpp
	${_keymap_dir}/KeyboardLayoutView.cpp
	${_keymap_dir}/Keymap.cpp
	${_keymap_dir}/KeymapListItem.cpp

	RDEF
	VirtualKeyboard.rdef
)

# -iquote: the preference's "Keymap.h" must win over headers/private/shared's
# on the global include path, which it includes itself as <Keymap.h>.
target_compile_options(virtualkeyboard PRIVATE "-iquote${_keymap_dir}")
target_compile_definitions(virtualkeyboard PRIVATE VIRTUAL_KEYBOARD_DEVICE)
UsePrivateHeaders(virtualkeyboard input interface shared system tracker)
