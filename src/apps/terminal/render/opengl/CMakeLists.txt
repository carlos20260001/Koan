# Copyright 2026, Koan contributors. All rights reserved.
# Distributed under the terms of the MIT License.

# Terminal's OpenGL renderer, an add-on so that Terminal runs where there is
# no OpenGL. Terminal loads it from next to itself or from
# <add-ons>/Terminal/Terminal-OpenGL.
# BGLView and the GL entry points: libGL-hgl when Mesa hgl is built
# (src/add-ons/opengl), otherwise the opengl kit and Debian's libGL.
if(TARGET GL-hgl)
	set(_gl_libs GL-hgl)
elseif(TARGET opengl)
	set(_gl_libs opengl ${GL_LIBRARIES})
else()
	message(STATUS "Terminal-OpenGL: skipped, needs opengl")
	return()
endif()

AddOn(Terminal-OpenGL
	SHARED

	SOURCES
	GLRenderer.cpp

	LIBS be root ${_gl_libs}
)
set_target_properties(Terminal-OpenGL PROPERTIES PREFIX "" SUFFIX "")
target_include_directories(Terminal-OpenGL PRIVATE
	${PROJECT_SOURCE_DIR}/src/apps/terminal/emulator
	${PROJECT_SOURCE_DIR}/src/apps/terminal/render
)
