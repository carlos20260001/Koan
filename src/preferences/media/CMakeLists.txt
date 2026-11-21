# Copyright 2026, Koan contributors. All rights reserved.
# Distributed under the terms of the MIT License.

# The classic Media Kit (libmedia) talks to media_server, whose PipeWire
# node add-on (src/add-ons/media/media-add-ons/pipewire) provides the
# devices; libmidi for the MIDI page.
Application(
	Media

	SOURCES
	Cards.cpp
	Glyphs.cpp
	LiveViews.cpp
	Media.cpp
	MediaModel.cpp
	MediaWindow.cpp
	Pages.cpp
	ParameterForm.cpp
	Sidebar.cpp

	LIBS media midi localestub
	RDEF media.rdef
)

UseMediaKit(Media)
UsePrivateHeaders(Media app shared)
# air/OS's MidiSettings; UsePrivateHeaders has no "midi" entry.
target_include_directories(Media PRIVATE
	${PROJECT_SOURCE_DIR}/headers/private/midi)
DoCatalogs("x-vnd.vos-Media" preferences/media)
