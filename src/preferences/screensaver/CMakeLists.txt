# Copyright 2026, Koan contributors. All rights reserved.
# Distributed under the terms of the MIT License.

if(NOT TARGET screensaver OR NOT TARGET game)
	return()
endif()

# ScreenSaver.cpp comes from the screen saver kit, for old add-ons that
# do not link libscreensaver themselves (as in air/OS's Jamfile).
Application(
	ScreenSaver

	SOURCES
	ScreenSaverApp.cpp
	ScreenSaverWindow.cpp
	PasswordWindow.cpp
	PreviewView.cpp
	ScreenCornerSelector.cpp
	${PROJECT_SOURCE_DIR}/src/kits/screensaver/ScreenSaver.cpp

	LIBS game screensaver tracker localestub crypt
	RDEF ScreenSaver.rdef
)

UsePrivateHeaders(ScreenSaver screen_saver)
target_include_directories(ScreenSaver PRIVATE
	${PROJECT_SOURCE_DIR}/headers/os/add-ons/screen_saver)
DoCatalogs("x-vnd.vos-ScreenSaver" preferences/screensaver)
