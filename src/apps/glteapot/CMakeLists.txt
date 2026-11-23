# Copyright 2026, Koan contributors. All rights reserved.
# Distributed under the terms of the MIT License.

# GLTeapot draws in a BGLView. It needs no GLU: ObjectView builds its
# perspective with glFrustum(). TeapotWindow is a BDirectWindow (libgame).
# BGLView, its headers and the GL entry points come from Mesa hgl's
# libGL-hgl (GL-hgl, src/add-ons/opengl).
foreach(_lib GL-hgl game localestub)
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

	LIBS GL-hgl game localestub
	RDEF GLTeapot.rdef
)

DoCatalogs("x-vnd.vos-GLTeapot" apps/glteapot)
