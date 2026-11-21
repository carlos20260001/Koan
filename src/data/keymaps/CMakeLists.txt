# Copyright 2026, Koan contributors. All rights reserved.
# Distributed under the terms of the MIT License.

# air/OS's keymaps: the host keymap compiler (src/tools/keymap, built in
# the buildtools pass) turns each .keymap source into the binary keymap
# the Keymap preference, FirstBootPrompt and the virtual keyboard read
# from /system/data/Keymaps. Three names are links to another keymap, as
# in Haiku.
set(_keymap_tool "${BUILDTOOLS_DIR}/src/tools/keymap/keymap")
if(NOT EXISTS "${_keymap_tool}")
	message(WARNING "No keymap compiler in ${BUILDTOOLS_DIR} (rebuild the "
		"buildtools): /system/data/Keymaps is not installed")
	return()
endif()

set(_keymap_out "${CMAKE_CURRENT_BINARY_DIR}/Keymaps")
file(GLOB _keymap_sources CONFIGURE_DEPENDS
	"${CMAKE_CURRENT_SOURCE_DIR}/*.keymap")
list(SORT _keymap_sources)

set(_keymap_outputs)
foreach(_source IN LISTS _keymap_sources)
	# Not NAME_WE: "Thai (TIS-820.2538)" has a dot in its name.
	get_filename_component(_name "${_source}" NAME)
	string(REGEX REPLACE "\\.keymap$" "" _name "${_name}")
	set(_binary "${_keymap_out}/${_name}")
	add_custom_command(
		OUTPUT "${_binary}"
		COMMAND ${CMAKE_COMMAND} -E make_directory "${_keymap_out}"
		COMMAND "${_keymap_tool}" -o "${_binary}" -c "${_source}"
		DEPENDS "${_source}" "${_keymap_tool}"
		COMMENT "Compiling keymap ${_name}"
		VERBATIM)
	list(APPEND _keymap_outputs "${_binary}")
endforeach()

add_custom_target(keymaps ALL DEPENDS ${_keymap_outputs})

install(DIRECTORY "${_keymap_out}/" DESTINATION /system/data/Keymaps
	COMPONENT data)

# alias : keymap
foreach(_alias Finnish:Swedish Croatian:Slovene Brazilian:US-International)
	string(REPLACE ":" ";" _pair "${_alias}")
	list(GET _pair 0 _link)
	list(GET _pair 1 _target)
	install(CODE "
		file(MAKE_DIRECTORY \"\$ENV{DESTDIR}/system/data/Keymaps\")
		file(CREATE_LINK \"${_target}\"
			\"\$ENV{DESTDIR}/system/data/Keymaps/${_link}\" SYMBOLIC)
	" COMPONENT data)
endforeach()
