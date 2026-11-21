# Copyright 2026, Koan contributors. All rights reserved.
# Distributed under the terms of the MIT License.

# janus_session starts FirstBootPrompt as the greeter frontend while
# /var/lib/vos/first-boot-done is missing (live media); it looks for it in
# /system/servers, /system/apps, then /system.
foreach(_lib translation shared localestub)
	if(NOT TARGET ${_lib})
		message(STATUS "FirstBootPrompt: skipped, needs ${_lib}")
		return()
	endif()
endforeach()

# KeymapNames.h marks the keymap names for translation, as air/OS's
# src/data/keymaps Jamfile generated it (same list as the Keymap panel).
set(_gen "${CMAKE_CURRENT_BINARY_DIR}/generated")
file(GLOB _keymap_files CONFIGURE_DEPENDS
	"${PROJECT_SOURCE_DIR}/src/data/keymaps/*.keymap")
set(_keymap_names Finnish Croatian Brazilian)
foreach(_file IN LISTS _keymap_files)
	get_filename_component(_name "${_file}" NAME_WE)
	list(APPEND _keymap_names "${_name}")
endforeach()
list(SORT _keymap_names)
set(_text "#undef B_TRANSLATION_CONTEXT\n#define B_TRANSLATION_CONTEXT \"KeymapNames\"\n")
foreach(_name IN LISTS _keymap_names)
	string(APPEND _text "B_TRANSLATE_MARK_VOID(\"${_name}\")\n")
endforeach()
file(CONFIGURE OUTPUT "${_gen}/KeymapNames.h" CONTENT "${_text}")

set(_keymap_dir "${PROJECT_SOURCE_DIR}/src/preferences/keymap")

Application(
	FirstBootPrompt

	SOURCES
	BootPrompt.cpp
	BootPromptWindow.cpp
	${_keymap_dir}/Keymap.cpp
	${_keymap_dir}/KeymapListItem.cpp

	LIBS translation shared localestub
	INCLUDES "${_gen}"
	RDEF BootPrompt.rdef
)

UsePrivateHeaders(FirstBootPrompt app interface locale shared system kernel
	input)
# The Keymap panel's "Keymap.h" (class Keymap) must win over
# headers/private/shared/Keymap.h (BKeymap) for quoted includes.
target_compile_options(FirstBootPrompt PRIVATE "-iquote${_keymap_dir}")
DoCatalogs("x-vnd.vos-FirstBootPrompt" apps/firstbootprompt)
