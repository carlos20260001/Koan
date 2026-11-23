# Copyright 2026, Koan contributors. All rights reserved.
# Distributed under the terms of the MIT License.

# Terminal's OpenGL renderer, an add-on so that Terminal runs where there is
# no OpenGL. Terminal loads it from next to itself or from
# <add-ons>/Terminal/Terminal-OpenGL.
# BGLView, its headers and the GL entry points come from Mesa hgl's
# libGL-hgl (GL-hgl, src/add-ons/opengl).
if(NOT TARGET GL-hgl)
	message(STATUS "Terminal-OpenGL: skipped, needs GL-hgl")
	return()
endif()

AddOn(Terminal-OpenGL
	SHARED

	SOURCES
	GLRenderer.cpp

	LIBS be root GL-hgl
)
set_target_properties(Terminal-OpenGL PROPERTIES PREFIX "" SUFFIX "")
target_include_directories(Terminal-OpenGL PRIVATE
	${PROJECT_SOURCE_DIR}/src/apps/terminal/emulator
	${PROJECT_SOURCE_DIR}/src/apps/terminal/render
)
