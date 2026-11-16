# ported subset: upstream adds apps, kits, libs, servers, system and vos.
# Each directory is added once its CMakeLists.txt exists; the others keep
# their Jamfiles until their wave ports them (PORTING.md M9). Tests that
# run on a plain Linux host are registered with CTest (ctest in this
# directory of the build tree); the others need nexus.ko and are only
# built.
enable_testing()

add_definitions(-DNO_ELF_SYMBOL_PATCHING)

include_directories("${PROJECT_SOURCE_DIR}/headers/tools/cppunit")

foreach(_dir apps kits libs servers system vos)
	if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${_dir}/CMakeLists.txt")
		add_subdirectory(${_dir})
	endif()
endforeach()
