# Copyright 2026, Koan contributors. All rights reserved.
# Distributed under the terms of the MIT License.

foreach(_lib shared translation localestub)
	if(NOT TARGET ${_lib})
		message(STATUS "LaunchBox: skipped, needs ${_lib}")
		return()
	endif()
endforeach()

Application(
	LaunchBox

	SOURCES
	App.cpp
	LaunchButton.cpp
	main.cpp
	MainWindow.cpp
	NamePanel.cpp
	PadView.cpp
	Panel.cpp
	support.cpp

	LIBS shared translation localestub
	RDEF LaunchBox.rdef
)

UsePrivateHeaders(LaunchBox shared)
DoCatalogs("x-vnd.vos-LaunchBox" apps/launchbox)
