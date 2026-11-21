# Copyright 2026, Koan contributors. All rights reserved.
# Distributed under the terms of the MIT License.

foreach(_lib tracker translation localestub)
	if(NOT TARGET ${_lib})
		message(STATUS "Screenshot: skipped, needs ${_lib}")
		return()
	endif()
endforeach()

# The GUI (Screenshot) and the command line tool (screenshot).
Application(
	Screenshot

	SOURCES
	ScreenshotApp.cpp
	ScreenshotWindow.cpp
	SelectAreaView.cpp
	Utility.cpp

	LIBS tracker translation localestub
	RDEF ScreenshotApp.rdef
)
UsePrivateHeaders(Screenshot interface shared)

# Both share the source name "Utility.cpp"; keep the CLI's objects apart.
Application(
	screenshot

	SOURCES
	Screenshot.cpp
	Utility.cpp

	LIBS translation localestub
	RDEF Screenshot.rdef
)
UsePrivateHeaders(screenshot interface shared)

DoCatalogs("x-vnd.vos-screenshot" apps/screenshot/Screenshot)
DoCatalogs("x-vnd.vos-screenshot-cli" apps/screenshot/screenshot)
