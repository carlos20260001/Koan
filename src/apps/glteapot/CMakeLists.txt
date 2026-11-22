# Copyright 2026, Koan contributors. All rights reserved.
# Distributed under the terms of the MIT License.

# GLTeapot draws with the OpenGL kit (BGLView on Mesa). It needs no GLU:
# ObjectView builds its perspective with glFrustum(). TeapotWindow is a
# BDirectWindow (libgame). With Mesa hgl built, BGLView and the GL entry
# points both come from libGL-hgl (src/add-ons/opengl); otherwise from the
# opengl kit and Debian's libGL.
if(TARGET GL-hgl)
	set(_gl_kit GL-hgl)
	set(_gl_libs GL-hgl)
else()
	set(_gl_kit opengl)
	set(_gl_libs opengl ${GL_LIBRARIES})
endif()
foreach(_lib ${_gl_kit} game localestub)
	if(NOT TARGET ${_lib})
		message(STATUS "GLTeapot: skipped, needs ${_lib}")
		return()
	endif()
endforeach()

Application(
	GLTeapot

	SOURCES
	FPS.cpp
	GLObject.cpp
	ObjectView.cpp
	error.cpp
	TeapotWindow.cpp
	TeapotApp.cpp

	LIBS ${_gl_libs} game localestub
	RDEF GLTeapot.rdef
)

DoCatalogs("x-vnd.vos-GLTeapot" apps/glteapot)
