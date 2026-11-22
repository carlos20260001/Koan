# Copyright 2026, Dario Casalinuovo. All rights reserved.
# Distributed under the terms of the MIT License.

# Screen saver add-ons. File name on disk is the menu label.
# Every saver links libscreensaver for BScreenSaver, as on air/OS.
macro(ScreenSaverAddOn name)
	AddOn(${name} SHARED ${ARGN})
	set_target_properties(${name} PROPERTIES PREFIX "" SUFFIX "")
	target_link_libraries(${name} PRIVATE screensaver)
	UsePrivateHeaders(${name} screen_saver interface)
	target_include_directories(${name} PRIVATE
		${PROJECT_SOURCE_DIR}/headers/os/add-ons/screen_saver
		${PROJECT_SOURCE_DIR}/headers/private/interface
		${PROJECT_SOURCE_DIR}/headers/private/shared
	)
endmacro()

if(NOT TARGET screensaver)
	message(STATUS "Screen savers: skipped, needs screensaver")
	return()
endif()

add_subdirectory(butterfly)
add_subdirectory(debugnow)
add_subdirectory(icons)
add_subdirectory(ifs)
add_subdirectory(message)
add_subdirectory(nebula)
add_subdirectory(shelf)
add_subdirectory(simpleclock)
add_subdirectory(slideshowsaver)
add_subdirectory(spider)

# The OpenGL savers draw in a BGLView, which comes from Mesa hgl: BGLView
# and the GL entry points are in libGL-hgl (src/add-ons/opengl), never in
# Debian's libGL. They use no GLU: the few GLU calls are done with GL.
if(TARGET GL-hgl)
	set(SCREEN_SAVER_GL_LIBS GL-hgl)
	add_subdirectory(flurry)
	add_subdirectory(glife)
	add_subdirectory(gravity)
else()
	message(STATUS "Screen savers flurry, glife, gravity: skipped, needs GL-hgl")
endif()

# leaves: not in this tree; air/OS dropped the Leaves saver.
