# Copyright 2026, Koan contributors. All rights reserved.
# Distributed under the terms of the MIT License.

# Runs the selected screen saver add-on full screen; ScreenSaver.cpp is
# libscreensaver's, linked in for add-ons that do not link the library.
Application(screen_blanker
	SOURCES
	ScreenBlanker.cpp
	ScreenSaverWindow.cpp
	PasswordWindow.cpp
	${PROJECT_SOURCE_DIR}/src/kits/screensaver/ScreenSaver.cpp

	LIBS game localestub screensaver crypt
	INCLUDES ${PROJECT_SOURCE_DIR}/headers/os/add-ons/screen_saver
	RDEF screen_blanker.rdef
)
UsePrivateHeaders(screen_blanker interface screen_saver)
DoCatalogs("x-vnd.vos.screenblanker" bin/screen_blanker)
