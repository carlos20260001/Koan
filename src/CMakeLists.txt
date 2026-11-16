if(NOT BUILDTOOLS_MODE STREQUAL "1")

add_subdirectory(system)
add_subdirectory(tools)

# ported subset: src/kits carries only be_bootstrap (linked into root) so
# far. bin, libs, servers, add-ons, apps, preferences, data and tests link
# against libbe, so they wait until the kits define the "be" target; kits is
# added first for that check. (src/bin already has a CMakeLists.txt, for the
# buildtools pass below.)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/kits/CMakeLists.txt")
	add_subdirectory(kits)
endif()

if(TARGET be)

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

else()

# add-ons/CMakeLists.txt leaves out what needs libbe; the optional Haiku GPU
# drivers (VOS_WITH_HAIKU_GPU_DRIVERS) do not need it.
add_subdirectory(add-ons)

endif()

else()

add_subdirectory(build)
add_subdirectory(bin)
add_subdirectory(tools)

endif()
