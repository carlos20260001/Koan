# Copyright 2026, Koan contributors. All rights reserved.
# Distributed under the terms of the MIT License.

foreach(_lib localestub translation)
	if(NOT TARGET ${_lib})
		message(STATUS "OverlayImage: skipped, needs ${_lib}")
		return()
	endif()
endforeach()

Application(
	OverlayImage

	SOURCES
	OverlayApp.cpp
	OverlayView.cpp
	OverlayWindow.cpp

	LIBS localestub translation
	RDEF OverlayImage.rdef
)

UsePrivateHeaders(OverlayImage interface shared)
DoCatalogs("x-vnd.vos-OverlayImage" apps/overlayimage)
