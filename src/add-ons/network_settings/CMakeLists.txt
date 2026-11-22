# Copyright 2026, Koan contributors.
# Distributed under the terms of the MIT License.

# The Network preference's add-ons, loaded from add-ons/Network Settings
# by file name. They subclass BNetworkSettingsAddOn and use the
# preference's shared views, which the Network executable exports: their
# remaining undefined symbols are resolved against it at load time.
# (src/preferences is configured after src/add-ons, hence no TARGET check.)
#
# Only hostname is ported; the others are open or replaced (PORTING.md M9,
# add-ons wave 3) and keep their Jamfiles.
macro(NetworkSettingsAddOn name)
	AddOn(${name} SHARED ${ARGN})
	set_target_properties(${name} PROPERTIES PREFIX "" SUFFIX "")
	target_link_libraries(${name} PRIVATE be root localestub)
	UsePrivateHeaders(${name} app net shared)
	target_include_directories(${name} PRIVATE
		${PROJECT_SOURCE_DIR}/headers/os/add-ons/network_settings
		${PROJECT_SOURCE_DIR}/src/preferences/network)
	add_dependencies(${name} Network)
endmacro()

add_subdirectory(hostname)

# CTest: what the Network preference looks up in each add-on.
enable_testing()
add_test(NAME network_settings_addons_entry_points
	COMMAND sh "${CMAKE_SOURCE_DIR}/src/tests/add-ons/check_exports.sh" "${CMAKE_OBJDUMP}"
		instantiate_network_settings_add_on
		-- $<TARGET_FILE:Hostname>)
