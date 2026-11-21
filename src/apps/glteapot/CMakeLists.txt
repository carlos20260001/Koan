# Copyright 2026, Koan contributors. All rights reserved.
# Distributed under the terms of the MIT License.

# GLTeapot draws with the OpenGL kit (BGLView on Mesa). It needs no GLU:
# ObjectView builds its perspective with glFrustum(). TeapotWindow is a
# BDirectWindow (libgame).
foreach(_lib opengl game localestub)
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

	LIBS opengl game localestub ${GL_LIBRARIES}
	RDEF GLTeapot.rdef
)

DoCatalogs("x-vnd.vos-GLTeapot" apps/glteapot)
