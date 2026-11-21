# Copyright 2026, Koan contributors. All rights reserved.
# Distributed under the terms of the MIT License.

# Mail provider infos: one empty file per provider whose attributes
# (from <provider>.rdef) hold its servers and options. E-mail's
# AutoConfig reads them from ~/config/settings/Mail/ProviderInfo, where
# air/OS's image put them. Git and install() keep no attributes, so the
# files are packed with their user.beos.* xattrs into
# /usr/share/vos/mail_provider_info.tar, like apps_attrs.tar.
# TODO(linux backend): unpack it into each user's settings at first
# login (data/system/boot/first_login).
if(BUILDTOOLS_DIR)
	set(_rc "${BUILDTOOLS_DIR}/src/bin/rc/rc")
	set(_resattr "${BUILDTOOLS_DIR}/src/bin/resattr")
else()
	set(_rc "rc")
	set(_resattr "resattr")
endif()

file(GLOB _provider_rdefs CONFIGURE_DEPENDS
	"${CMAKE_CURRENT_SOURCE_DIR}/*.rdef")
list(SORT _provider_rdefs)

set(_stage "${CMAKE_CURRENT_BINARY_DIR}/stage")
set(_tar "${CMAKE_BINARY_DIR}/mail_provider_info.tar")
set(_cmds
	COMMAND ${CMAKE_COMMAND} -E rm -rf "${_stage}"
	COMMAND ${CMAKE_COMMAND} -E make_directory "${_stage}")
foreach(_rdef IN LISTS _provider_rdefs)
	get_filename_component(_file "${_rdef}" NAME)
	string(REGEX REPLACE "\\.rdef$" "" _provider "${_file}")
	set(_rsrc "${CMAKE_CURRENT_BINARY_DIR}/${_provider}.rsrc")
	list(APPEND _cmds
		COMMAND "${_rc}" "${_rdef}" -o "${_rsrc}"
		COMMAND ${CMAKE_COMMAND} -E touch "${_stage}/${_provider}"
		COMMAND "${_resattr}" -O -o "${_stage}/${_provider}" "${_rsrc}")
endforeach()
list(APPEND _cmds
	COMMAND tar --xattrs -cf "${_tar}" -C "${_stage}" .)

add_custom_command(
	OUTPUT "${_tar}"
	${_cmds}
	DEPENDS ${_provider_rdefs}
	COMMENT "Packing the mail provider infos"
)
add_custom_target(mail_provider_info ALL DEPENDS "${_tar}")

install(FILES "${_tar}" DESTINATION /usr/share/vos COMPONENT data)
