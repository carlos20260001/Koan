# Copyright 2026, Koan contributors. All rights reserved.
# Distributed under the terms of the MIT License.

# The classic Media Kit (libmedia) talks to media_server, whose PipeWire
# node add-on provides the capture and playback devices.
foreach(_lib media tracker localestub)
	if(NOT TARGET ${_lib})
		message(STATUS "SoundRecorder: skipped, needs ${_lib}")
		return()
	endif()
endforeach()

Application(
	SoundRecorder

	SOURCES
	DrawButton.cpp
	DrawingTidbits.cpp
	FileUtils.cpp
	RecorderApp.cpp
	RecorderWindow.cpp
	ScopeView.cpp
	SoundListView.cpp
	TrackSlider.cpp
	TransportButton.cpp
	UpDownButton.cpp
	VUView.cpp
	VolumeSlider.cpp

	LIBS media tracker localestub
	RDEF SoundRecorder.rdef
)

UseMediaKit(SoundRecorder)
UsePrivateHeaders(SoundRecorder shared)

DoCatalogs("x-vnd.vos-SoundRecorder" apps/soundrecorder)
