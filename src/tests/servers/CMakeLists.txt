# Each server's tests are added once their directory has a CMakeLists.txt.
file(GLOB _vos_server_test_dirs LIST_DIRECTORIES true
	RELATIVE "${CMAKE_CURRENT_SOURCE_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}/*")
list(SORT _vos_server_test_dirs)
foreach(_dir ${_vos_server_test_dirs})
	if(IS_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/${_dir}"
			AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${_dir}/CMakeLists.txt")
		add_subdirectory(${_dir})
	endif()
endforeach()
