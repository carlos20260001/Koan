# Copyright 2026, Koan contributors. All rights reserved.
# Distributed under the terms of the MIT License.
#
# TV: shows a video capture node (a tuner or camera media add-on, such as
# air/OS's UVC add-on) through the classic Media Kit and media_server.

foreach(_lib media localestub)
	if(NOT TARGET ${_lib})
		message(STATUS "TV: skipped, needs ${_lib}")
		return()
	endif()
endforeach()

Application(
	TV

	SOURCES
	Controller.cpp
	ConvertBitmap.cpp
	DeviceRoster.cpp
	MainApp.cpp
	MainWin.cpp
	VideoNode.cpp
	VideoView.cpp

	LIBS media localestub
	RDEF tv.rdef
)

UseMediaKit(TV)
DoCatalogs("x-vnd.vos.TV" apps/tv)
