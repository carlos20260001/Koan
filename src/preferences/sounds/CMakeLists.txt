# Copyright 2026, Koan contributors. All rights reserved.
# Distributed under the terms of the MIT License.

# Sounds edits the system beep events of the classic Media Kit.
if(NOT TARGET media OR NOT TARGET game)
	return()
endif()

Application(
	Sounds

	SOURCES
	HApp.cpp
	HWindow.cpp
	HEventList.cpp
	SoundFilePanel.cpp

	LIBS game tracker media localestub
	RDEF Sounds.rdef
)

UseMediaKit(Sounds)
UsePrivateHeaders(Sounds interface)
DoCatalogs("x-vnd.vos-Sounds" preferences/sounds)
