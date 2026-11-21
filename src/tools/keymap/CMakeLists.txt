# Copyright 2026, Koan contributors. All rights reserved.
# Distributed under the terms of the MIT License.

# Host keymap compiler (Haiku's <build>keymap): `keymap -c` turns the
# src/data/keymaps sources into the binary keymaps the Keymap preference,
# FirstBootPrompt and the virtual keyboard read from /system/data/Keymaps.
# Built in the buildtools pass with the host libbe, like rc and xres.
if(BUILDTOOLS_MODE STREQUAL "1")

set(_keymap_src ${CMAKE_SOURCE_DIR}/src/bin/keymap)

BuildModeApplication(keymap
	SOURCES
	${_keymap_src}/main.cpp
	${_keymap_src}/Keymap.cpp
	${CMAKE_SOURCE_DIR}/src/kits/shared/Keymap.cpp
	input_globals_build.cpp
)

UsePrivateBuildHeaders(keymap shared storage)
UsePrivateHeaders(keymap interface)

endif()
