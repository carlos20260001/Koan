# Copyright 2026, Koan contributors.
# Distributed under the terms of the MIT License.

# quick_look_server's previewers. The server loads every add-on in
# add-ons/quick_look and asks it for instantiate_quick_look_add_on().
macro(QuickLookAddOn name)
	AddOn(${name} SHARED ${ARGN})
	set_target_properties(${name} PROPERTIES PREFIX "" SUFFIX "")
	target_link_libraries(${name} PRIVATE quicklook be root localestub)
	target_link_options(${name} PRIVATE "-Wl,--no-undefined")
	UsePrivateHeaders(${name} shared interface)
	target_include_directories(${name} PRIVATE
		${PROJECT_SOURCE_DIR}/headers/os/add-ons/quick_look)
endmacro()

add_subdirectory(image)
add_subdirectory(pdf)
add_subdirectory(text)

# CTest: what quick_look_server looks up in each add-on.
enable_testing()
add_test(NAME quick_look_addons_entry_points
	COMMAND sh "${CMAKE_SOURCE_DIR}/src/tests/add-ons/check_exports.sh" "${CMAKE_OBJDUMP}"
		instantiate_quick_look_add_on
		-- $<TARGET_FILE:ImagePreview> $<TARGET_FILE:PDFPreview>
		$<TARGET_FILE:TextPreview>)
