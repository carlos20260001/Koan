# ported subset: upstream adds every library test directory. Each one is
# added once its CMakeLists.txt exists; the others keep their Jamfiles
# until their wave ports them (PORTING.md M9).
foreach(_lib backlight)
	if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${_lib}/CMakeLists.txt")
		add_subdirectory(${_lib})
	endif()
endforeach()
