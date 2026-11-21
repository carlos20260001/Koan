# Copyright 2026, Koan contributors. All rights reserved.
# Distributed under the terms of the MIT License.

Application(
	Input

	SOURCES
	Input.cpp
	InputDeviceView.cpp
	InputIcons.cpp
	InputKeyboard.cpp
	InputMouse.cpp
	InputTouchpadPref.cpp
	InputTouchpadPrefView.cpp
	InputWindow.cpp
	KeyboardSettings.cpp
	KeyboardView.cpp
	MouseSettings.cpp
	MouseView.cpp
	SettingsView.cpp

	LIBS localestub
	RDEF Input.rdef
)

UsePrivateHeaders(Input input)
DoCatalogs("x-vnd.vos-Input" preferences/input)
