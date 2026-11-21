# Copyright 2026, Koan contributors. All rights reserved.
# Distributed under the terms of the MIT License.

# BJoystick (evdev) lives in libgame here.
if(NOT TARGET game)
	return()
endif()

Application(
	Joysticks

	SOURCES
	CalibWin.cpp
	JoyWin.cpp
	Joysticks.cpp
	MessageWin.cpp
	PortItem.cpp

	LIBS game
	RDEF Joysticks.rdef
)
