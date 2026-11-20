# Each server is added once its directory has a CMakeLists.txt (M9 W3:
# servers are ported in parallel; the Jamfile goes when CMake builds it).
file(GLOB _vos_server_dirs LIST_DIRECTORIES true
	RELATIVE "${CMAKE_CURRENT_SOURCE_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}/*")
list(SORT _vos_server_dirs)
foreach(_dir ${_vos_server_dirs})
	if(IS_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/${_dir}"
			AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${_dir}/CMakeLists.txt")
		add_subdirectory(${_dir})
	endif()
endforeach()
