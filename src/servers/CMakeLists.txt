# ported subset: every server directory is added once it has a
# CMakeLists.txt; the others keep their Jamfiles until their wave ports them
# (PORTING.md M9). VitruvianOS's session servers come first, in its order,
# then the rest alphabetically, so a new port needs no change here.
# Adds headers/private/<name> for each name to <target>: UsePrivateHeaders()
# knows only the directories VitruvianOS has, not air/OS's (media, midi,
# bluetooth, osd, quicklook, ...). Used by the air/OS servers (W3-D).
function(VosPrivateHeaders target)
	UsePrivateHeaders(${target} ${ARGN})
	foreach(_dir IN LISTS ARGN)
		target_include_directories(${target} PRIVATE
			"${PROJECT_SOURCE_DIR}/headers/private/${_dir}")
	endforeach()
endfunction()

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
