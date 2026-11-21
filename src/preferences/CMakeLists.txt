# Copyright 2026, Koan contributors. All rights reserved.
# Distributed under the terms of the MIT License.

# The preference panels. Each panel directory is added once its
# CMakeLists.txt is in the tree, so panels land in any order; a panel
# whose kit libraries are missing leaves itself out (see its own
# CMakeLists.txt). Install lists: build/profiles/preferences.cmake.
file(GLOB _pref_dirs LIST_DIRECTORIES true RELATIVE "${CMAKE_CURRENT_SOURCE_DIR}"
	"${CMAKE_CURRENT_SOURCE_DIR}/*")
list(SORT _pref_dirs)
foreach(_dir IN LISTS _pref_dirs)
	if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${_dir}/CMakeLists.txt")
		add_subdirectory(${_dir})
	endif()
endforeach()
