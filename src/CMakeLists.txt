if(NOT BUILDTOOLS_MODE STREQUAL "1")

add_subdirectory(system)
add_subdirectory(tools)

# ported subset: src/kits builds be_bootstrap (linked into root) and a
# partial "be" (support, app and storage kits, W1-K1). bin, libs, servers,
# add-ons, apps, preferences, data and tests also need the kits W2 ports
# (interface, locale, localestub, shared, ...), so they wait until those
# define their targets; kits is added first for that check. (src/bin
# already has a CMakeLists.txt, for the buildtools pass below.)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/kits/CMakeLists.txt")
	add_subdirectory(kits)
endif()

if(TARGET be AND TARGET localestub)

add_subdirectory(bin)
add_subdirectory(libs)
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
