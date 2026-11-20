# ported subset: each server test directory is added once it has a
# CMakeLists.txt; the others keep their Jamfiles until their wave ports
# them (PORTING.md M9).
file(GLOB _vos_server_tests LIST_DIRECTORIES true
	RELATIVE "${CMAKE_CURRENT_SOURCE_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}/*")
list(SORT _vos_server_tests)

foreach(_dir ${_vos_server_tests})
	if(IS_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/${_dir}"
			AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${_dir}/CMakeLists.txt")
		add_subdirectory(${_dir})
	endif()
endforeach()
