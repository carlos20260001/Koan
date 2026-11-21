# Copyright 2026, Koan contributors. All rights reserved.
# Distributed under the terms of the MIT License.

# Pulse shows the load of every CPU from libroot2's get_system_info() and
# get_cpu_info(), which read /proc. DeskbarPulseView is its Deskbar
# replicant (archived into the Deskbar from the same binary).
if(NOT TARGET localestub)
	message(STATUS "Pulse: skipped, needs localestub")
	return()
endif()

Application(
	Pulse

	SOURCES
	ConfigView.cpp
	CPUButton.cpp
	DeskbarPulseView.cpp
	MiniPulseView.cpp
	NormalPulseView.cpp
	Prefs.cpp
	PrefsWindow.cpp
	ProgressBar.cpp
	PulseApp.cpp
	PulseView.cpp
	PulseWindow.cpp

	LIBS localestub
	RDEF Pulse.rdef
)

UsePrivateHeaders(Pulse system shared interface app)
DoCatalogs("x-vnd.vos-Pulse" apps/pulse)
