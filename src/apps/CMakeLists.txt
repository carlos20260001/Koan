# ported subset: only the apps whose VitruvianOS CMakeLists.txt is in this
# tree. src/CMakeLists.txt adds this directory once the kits define "be";
# each app also waits for the other kit libraries it links (LIBS in its
# CMakeLists.txt), which arrive with the same kits milestone.
if(TARGET tracker AND TARGET shared AND TARGET localestub)
	# The desktop shell. Each is added once its CMakeLists.txt is in the
	# tree, so the Deskbar and Tracker ports can land in either order.
	foreach(_shell_dir deskbar tracker)
		if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${_shell_dir}/CMakeLists.txt")
			add_subdirectory(${_shell_dir})
		endif()
	endforeach()
	add_subdirectory(drivesetup)
	if(TARGET translation)
		add_subdirectory(installer)
	endif()
endif()
if(TARGET shared AND TARGET localestub)
	add_subdirectory(vos-polkit-agent)
endif()
add_subdirectory(vos-polkit-grant-helper)

# Every other app directory that has a CMakeLists.txt (M9 W4 app ports). Each
# app's own CMakeLists.txt checks the library targets it links and skips
# itself with a message when one is missing, so ports can land in any order
# without editing this file.
set(_vos_apps_listed deskbar tracker drivesetup installer vos-polkit-agent
	vos-polkit-grant-helper)
file(GLOB _vos_app_dirs LIST_DIRECTORIES true
	RELATIVE "${CMAKE_CURRENT_SOURCE_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}/*")
list(SORT _vos_app_dirs)
foreach(_dir IN LISTS _vos_app_dirs)
	if(_dir IN_LIST _vos_apps_listed)
		continue()
	endif()
	if(IS_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/${_dir}"
			AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${_dir}/CMakeLists.txt")
		add_subdirectory(${_dir})
	endif()
endforeach()
