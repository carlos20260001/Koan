# Copyright 2026, Koan contributors. All rights reserved.
# Distributed under the terms of the MIT License.

# PatchBay connects MIDI producers and consumers through the MIDI kits
# (libmidi2's BMidiRoster, whose endpoints are PipeWire MIDI ports).
foreach(_lib midi midi2 icon localestub)
	if(NOT TARGET ${_lib})
		message(STATUS "PatchBay: skipped, needs ${_lib}")
		return()
	endif()
endforeach()

Application(
	PatchBay

	SOURCES
	PatchApp.cpp
	PatchWin.cpp
	PatchView.cpp
	PatchRow.cpp
	EndpointInfo.cpp
	MidiEventMeter.cpp

	LIBS midi midi2 icon localestub
	RDEF PatchBay.rdef
)

DoCatalogs("x-vnd.vos.PatchBay" apps/patchbay)
