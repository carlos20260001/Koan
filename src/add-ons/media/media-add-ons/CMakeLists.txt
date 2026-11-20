# Classic Media Kit node add-ons (libmedia). Each subdirectory is added once
# its CMakeLists.txt exists; the rest come with W4 (PORTING.md M9).
foreach(_dir pipewire)
	if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${_dir}/CMakeLists.txt")
		add_subdirectory(${_dir})
	endif()
endforeach()
