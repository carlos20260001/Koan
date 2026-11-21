# Copyright 2026, Koan contributors. All rights reserved.
# Distributed under the terms of the MIT License.

# A Deskbar applet and an application in one file (RunnableAddOn): Deskbar
# loads it with load_add_on() and calls instantiate_deskbar_item(). Teams,
# threads, areas and images come from libroot2, which reads /proc; the
# scheduler modes are power-profiles-daemon's profiles (SchedulerMode.cpp).
foreach(_lib shared localestub)
	if(NOT TARGET ${_lib})
		message(STATUS "ProcessController: skipped, needs ${_lib}")
		return()
	endif()
endforeach()

RunnableAddOn(
	ProcessController

	SOURCES
	AutoIcon.cpp
	IconMenuItem.cpp
	KernelMemoryBarMenuItem.cpp
	MemoryBarMenu.cpp
	MemoryBarMenuItem.cpp
	NoiseBarMenuItem.cpp
	ProcessController.cpp
	PCWindow.cpp
	PCWorld.cpp
	Preferences.cpp
	PriorityMenu.cpp
	SchedulerMode.cpp
	QuitMenu.cpp
	TeamBarMenu.cpp
	TeamBarMenuItem.cpp
	ThreadBarMenu.cpp
	ThreadBarMenuItem.cpp
	Utilities.cpp

	LIBS shared localestub systemd
	RDEF ProcessController.rdef
)

UsePrivateHeaders(ProcessController app interface shared system)
DoCatalogs("x-vnd.vos-ProcessController" apps/processcontroller)
