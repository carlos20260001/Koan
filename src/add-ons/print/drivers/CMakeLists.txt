# Copyright 2026, Koan contributors.
# Distributed under the terms of the MIT License.

# Printer drivers on libprint.a (src/libs/print/libprint).
macro(PrintDriverAddOn target output)
	PrintAddOn(${target} "${output}" ${ARGN})
	target_link_libraries(${target} PRIVATE libprint)
	# BEFORE: src/kits/tracker on the global include path has headers of
	# the same names (StatusWindow.h).
	target_include_directories(${target} BEFORE PRIVATE
		${PROJECT_SOURCE_DIR}/headers/libs/print/libprint)
endmacro()

add_subdirectory(canon_lips)
add_subdirectory(gutenprint)
add_subdirectory(lpstyl)
add_subdirectory(pcl5)
add_subdirectory(pcl6)
add_subdirectory(postscript)
add_subdirectory(preview)
