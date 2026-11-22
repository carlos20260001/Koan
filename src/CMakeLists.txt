if(NOT BUILDTOOLS_MODE STREQUAL "1")

# Declared before any subdirectory reads it (system/boot_splash builds its
# host test by default when tests are on).
option(VOS_BUILD_TESTS "Build the tests in a non-Debug build too" OFF)

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

# ported subset: each of these is added once its CMakeLists.txt exists
# (W2 adds libs; servers, preferences, data and tests come with W3/W4).
# src/bin comes after the kits and libs its commands link (tracker, mail,
# media, screensaver, ...).
foreach(_dir libs servers add-ons apps preferences bin data)
	if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${_dir}/CMakeLists.txt")
		add_subdirectory(${_dir})
	endif()
endforeach()

# Debug builds add the tests; VOS_BUILD_TESTS (declared at the top)
# adds them to other builds too.
if((CMAKE_BUILD_TYPE STREQUAL "Debug" OR VOS_BUILD_TESTS)
		AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/CMakeLists.txt")
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
