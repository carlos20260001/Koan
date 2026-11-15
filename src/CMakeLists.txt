if(NOT BUILDTOOLS_MODE STREQUAL "1")

add_subdirectory(system)
add_subdirectory(tools)

# ported subset: bin, libs, kits, servers, add-ons, apps, preferences, data
# and tests link against libbe, so they wait until src/kits is on CMake.
# (src/bin already has a CMakeLists.txt, for the buildtools pass below.)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/kits/CMakeLists.txt")

add_subdirectory(bin)
add_subdirectory(libs)
add_subdirectory(kits)
add_subdirectory(servers)
add_subdirectory(add-ons)
add_subdirectory(apps)
add_subdirectory(preferences)
add_subdirectory(data)

if(CMAKE_BUILD_TYPE STREQUAL "Debug")
	add_subdirectory(tests)
endif()

endif()

else()

add_subdirectory(build)
add_subdirectory(bin)
add_subdirectory(tools)

endif()
