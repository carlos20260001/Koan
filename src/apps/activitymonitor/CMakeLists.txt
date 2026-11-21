# Copyright 2026, Koan contributors. All rights reserved.
# Distributed under the terms of the MIT License.

foreach(_lib tracker media shared localestub)
	if(NOT TARGET ${_lib})
		message(STATUS "ActivityMonitor: skipped, needs ${_lib}")
		return()
	endif()
endforeach()

Application(
	ActivityMonitor

	SOURCES
	ActivityMonitor.cpp
	ActivityView.cpp
	ActivityWindow.cpp
	DataSource.cpp
	SettingsWindow.cpp
	SystemInfo.cpp
	SystemInfoHandler.cpp

	LIBS tracker media shared localestub
	RDEF ActivityMonitor.rdef
)

UsePrivateHeaders(ActivityMonitor interface shared system kernel)
DoCatalogs("x-vnd.vos-ActivityMonitor" apps/activitymonitor)
# Classic Media Kit headers (media node counts), as the Media panel uses.
UseMediaKit(ActivityMonitor)
# air/OS's sources keep a BeOS R5 fallback behind #ifdef __HAIKU__ (no
# layouts, BControlLook or BAboutWindow); this tree's kits are the Haiku API.
target_compile_definitions(ActivityMonitor PRIVATE __HAIKU__=1)
