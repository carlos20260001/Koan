# Copyright 2026, Koan contributors. All rights reserved.
# Distributed under the terms of the MIT License.

# air/OS's System Updater. The network kit (libnetapi, libnetservices) and
# BColumnListView are in libbe, BPrivate::SHA256 in libroot; the installed
# packages come from the hpkg package kit (libpackage, target "bpackage").
foreach(_lib bpackage shared localestub)
	if(NOT TARGET ${_lib})
		message(STATUS "SystemUpdater: skipped, needs ${_lib}")
		return()
	endif()
endforeach()

Application(
	SystemUpdater

	SOURCES
	CommandLine.cpp
	Desktop.cpp
	Feed.cpp
	InstallWindow.cpp
	PiBoot.cpp
	Release.cpp
	Settings.cpp
	SystemState.cpp
	SystemUpdaterApp.cpp
	Updater.cpp
	UpdaterWindow.cpp
	UpdateWorker.cpp

	LIBS bpackage shared localestub
	RDEF SystemUpdater.rdef
)

UsePrivateHeaders(SystemUpdater app interface kernel libroot screen_saver
	shared storage system)
target_include_directories(SystemUpdater PRIVATE
	${PROJECT_SOURCE_DIR}/headers/private/netservices
	${PROJECT_SOURCE_DIR}/headers/private/package
)
