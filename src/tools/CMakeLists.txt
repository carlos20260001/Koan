if(NOT BUILDTOOLS_MODE STREQUAL "1")

UsePrivateHeaders(GLOBAL app storage support shared)

#Application(cpuidtool                 SOURCES cpuidtool.c)
#Application(create_image              SOURCES create_image.cpp)
#Application(data_to_source            SOURCES data_to_source.cpp)
#Application(elf2aout                  SOURCES elf2aout.c)
#Application(exec                      SOURCES exec.c)
#Application(generate_attribute_stores SOURCES generate_attribute_stores.cpp)
#Application(rm_attrs                  SOURCES rm_attrs.cpp)

# ported subset: cppunit and dh-vos are not in this tree yet.
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/cppunit/CMakeLists.txt")
	add_subdirectory(cppunit)
endif()
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/dh-vos/CMakeLists.txt")
	add_subdirectory(dh-vos)
endif()
add_subdirectory(locale)

# Boot splash image header generator (Haiku's host tool), for the vos-splash
# artwork: src/system/boot_splash/tools/make-koan-images.sh builds and runs
# it. Not built by default and not installed.
find_package(PNG)
if(PNG_FOUND)
	add_executable(generate_boot_screen EXCLUDE_FROM_ALL
		generate_boot_screen.cpp
		${PROJECT_SOURCE_DIR}/src/kits/shared/ColorQuantizer.cpp)
	target_link_libraries(generate_boot_screen PRIVATE PNG::PNG ZLIB::ZLIB)
	# As engine.cmake's targets do: the Haiku types and __VOS__ come first.
	set_target_properties(generate_boot_screen PROPERTIES
		COMPILE_FLAGS "-include LinuxBuildCompatibility.h")
endif()
#add_subdirectory(hvif2png)
#add_subdirectory(MsgSpy)
#add_subdirectory(restest)
#add_subdirectory(translation)
#add_subdirectory(unflatten)

else()

add_subdirectory(keymap)
add_subdirectory(locale)

endif()
