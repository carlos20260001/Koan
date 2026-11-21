# Copyright 2026, Koan contributors. All rights reserved.
# Distributed under the terms of the MIT License.

# libmidi/libmidi2 (the MIDI kits on fluidsynth).
foreach(_lib midi midi2 localestub)
	if(NOT TARGET ${_lib})
		message(STATUS "MidiPlayer: skipped, needs ${_lib}")
		return()
	endif()
endforeach()

Application(
	MidiPlayer

	SOURCES
	MidiPlayerApp.cpp
	MidiPlayerWindow.cpp
	ScopeView.cpp
	SynthBridge.cpp

	LIBS midi midi2 localestub
	RDEF MidiPlayer.rdef
)

UsePrivateHeaders(MidiPlayer interface)

DoCatalogs("x-vnd.vos-MidiPlayer" apps/midiplayer)
