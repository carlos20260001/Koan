# Copyright 2026, Koan contributors. All rights reserved.
# Distributed under the terms of the MIT License.

# KeymapNames.h and KeyboardLayoutNames.h mark the keymap and keyboard
# layout names for translation (B_TRANSLATE_MARK_VOID), as air/OS's
# src/data/keymaps and src/data/keyboard_layouts Jamfiles generated them.
# The names are the keymap files in src/data/keymaps (plus the aliases
# Jam added) and the layout files in data/system/data/KeyboardLayouts,
# whose two subfolders become submenus.
set(_gen "${CMAKE_CURRENT_BINARY_DIR}/generated")

file(GLOB _keymap_files CONFIGURE_DEPENDS
	"${PROJECT_SOURCE_DIR}/src/data/keymaps/*.keymap")
set(_keymap_names "")
foreach(_file IN LISTS _keymap_files)
	get_filename_component(_name "${_file}" NAME_WE)
	list(APPEND _keymap_names "${_name}")
endforeach()
list(APPEND _keymap_names Finnish Croatian Brazilian)
list(SORT _keymap_names)
set(_text "#undef B_TRANSLATION_CONTEXT\n#define B_TRANSLATION_CONTEXT \"KeymapNames\"\n")
foreach(_name IN LISTS _keymap_names)
	string(APPEND _text "B_TRANSLATE_MARK_VOID(\"${_name}\")\n")
endforeach()
file(CONFIGURE OUTPUT "${_gen}/KeymapNames.h" CONTENT "${_text}")

set(_layouts_dir "${PROJECT_SOURCE_DIR}/data/system/data/KeyboardLayouts")
file(GLOB_RECURSE _layout_files CONFIGURE_DEPENDS
	LIST_DIRECTORIES true RELATIVE "${_layouts_dir}" "${_layouts_dir}/*")
list(SORT _layout_files)
set(_text "#undef B_TRANSLATION_CONTEXT\n#define B_TRANSLATION_CONTEXT \"KeyboardLayoutNames\"\n")
foreach(_entry IN LISTS _layout_files)
	get_filename_component(_name "${_entry}" NAME)
	string(APPEND _text "B_TRANSLATE_MARK_VOID(\"${_name}\")\n")
endforeach()
file(CONFIGURE OUTPUT "${_gen}/KeyboardLayoutNames.h" CONTENT "${_text}")

Application(
	Keymap

	SOURCES
	KeyboardLayout.cpp
	KeyboardLayoutView.cpp
	Keymap.cpp
	KeymapApplication.cpp
	KeymapListItem.cpp
	KeymapWindow.cpp
	ModifierKeysWindow.cpp
	StatusMenuField.cpp

	LIBS tracker shared localestub
	RDEF Keymap.rdef
)

UsePrivateHeaders(Keymap interface shared input)
target_include_directories(Keymap PRIVATE "${_gen}")
DoCatalogs("x-vnd.vos-Keymap" preferences/keymap)
