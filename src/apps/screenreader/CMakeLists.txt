# Copyright 2026, Koan contributors. All rights reserved.
# Distributed under the terms of the MIT License.

Application(
	ScreenReader

	SOURCES
	CaptionWindow.cpp
	NodeText.cpp
	ScreenReader.cpp
	SpeechOutput.cpp

	RDEF ScreenReader.rdef
)

UsePrivateHeaders(ScreenReader app interface)
