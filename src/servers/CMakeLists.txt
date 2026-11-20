# ported subset: every server directory is added once it has a
# CMakeLists.txt; the others keep their Jamfiles until their wave ports them
# (PORTING.md M9). VitruvianOS's session servers come first, in its order,
# then the rest alphabetically, so a new port needs no change here.
set(_vos_servers_first
	janus
	janus_session
	app
	input
	mount
	registrar
	notification
	keystore
)

file(GLOB _vos_servers_all LIST_DIRECTORIES true
	RELATIVE "${CMAKE_CURRENT_SOURCE_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}/*")
list(SORT _vos_servers_all)

set(_vos_servers ${_vos_servers_first} ${_vos_servers_all})
list(REMOVE_DUPLICATES _vos_servers)

foreach(_dir ${_vos_servers})
	if(IS_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/${_dir}"
			AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${_dir}/CMakeLists.txt")
		add_subdirectory(${_dir})
	endif()
endforeach()
