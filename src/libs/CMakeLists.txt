# ported subset: upstream adds every library unconditionally. Each one is
# added here once its CMakeLists.txt exists; the others keep their Jamfiles
# until their wave ports them (PORTING.md M9).
foreach(_lib
		agg alm backlight icon kdbx linprog libscan mapm outputaddon prefs
		print printcups xclient)
	if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${_lib}/CMakeLists.txt")
		add_subdirectory(${_lib})
	endif()
endforeach()
