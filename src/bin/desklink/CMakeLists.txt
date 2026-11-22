# Copyright 2026, Koan contributors. All rights reserved.
# Distributed under the terms of the MIT License.

Application(desklink
	SOURCES
	desklink.cpp
	AudioDevices.cpp
	DeskButton.cpp
	MediaReplicant.cpp
	MixerControl.cpp
	VolumeControl.cpp
	VolumeWindow.cpp

	LIBS media localestub
	RDEF desklink.rdef SpeakerBox.rdef
)
UseMediaKit(desklink)
UsePrivateHeaders(desklink app interface locale shared)
DoCatalogs("x-vnd.vos-desklink" bin/desklink)
