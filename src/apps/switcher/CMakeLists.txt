# Copyright 2026, Koan contributors. All rights reserved.
# Distributed under the terms of the MIT License.

foreach(_lib shared translation localestub)
	if(NOT TARGET ${_lib})
		message(STATUS "Switcher: skipped, needs ${_lib}")
		return()
	endif()
endforeach()

Application(
	Switcher

	SOURCES
	ApplicationsView.cpp
	CaptureWindow.cpp
	GroupListView.cpp
	LaunchButton.cpp
	PanelWindow.cpp
	Switcher.cpp
	WindowsView.cpp

	LIBS shared translation localestub
	RDEF Switcher.rdef
)

UsePrivateHeaders(Switcher app interface shared)
