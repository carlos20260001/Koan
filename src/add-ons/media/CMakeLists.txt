# Media add-ons. Each subdirectory is added once its CMakeLists.txt exists;
# the others keep their Jamfiles until their wave ports them (PORTING.md M9).
foreach(_dir media-add-ons)
	if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${_dir}/CMakeLists.txt")
		add_subdirectory(${_dir})
	endif()
endforeach()
