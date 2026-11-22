# Copyright 2026, Koan contributors.
# Distributed under the terms of the MIT License.

# DVD navigation on Debian's libdvdnav (LIBDVDNAV in build/deps.cmake).
find_package(PkgConfig REQUIRED)
pkg_check_modules(DVDNAV REQUIRED dvdnav)

MediaKitAddOn(dvd_streamer
	SOURCES
	DVDStreamerPlugin.cpp
	DVDMediaIO.cpp

	LIBS ${DVDNAV_LIBRARIES}
)
target_include_directories(dvd_streamer PRIVATE ${DVDNAV_INCLUDE_DIRS})
target_compile_options(dvd_streamer PRIVATE ${DVDNAV_CFLAGS_OTHER})
