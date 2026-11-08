if(NOT BUILDTOOLS_MODE STREQUAL "1")

add_subdirectory(system)
add_subdirectory(bin)
add_subdirectory(tools)
add_subdirectory(libs)
add_subdirectory(kits)

# wlroots-koan must be processed before servers so that app_server can
# link against the wlroots static library. When VOS_WITH_WLROOTS is ON,
# add it here and skip it in add-ons/CMakeLists.txt.
if(VOS_WITH_WLROOTS)
	add_subdirectory(add-ons/wlroots-koan)
	# Directory-scoped flag consumed by add-ons/CMakeLists.txt. Do not cache
	# it: a stale cache entry would survive toggling VOS_WITH_WLROOTS.
	set(WLROOTS_KOAN_ADDED TRUE)
endif()

add_subdirectory(servers)
add_subdirectory(add-ons)
add_subdirectory(apps)
add_subdirectory(preferences)
add_subdirectory(data)

if(CMAKE_BUILD_TYPE STREQUAL "Debug")
	add_subdirectory(tests)
endif()

else()

add_subdirectory(build)
add_subdirectory(bin)
add_subdirectory(tools)

endif()
