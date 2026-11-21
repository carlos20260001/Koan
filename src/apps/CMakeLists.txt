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
