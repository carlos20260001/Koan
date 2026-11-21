# Copyright 2026, Koan contributors. All rights reserved.
# Distributed under the terms of the MIT License.

foreach(_lib shared localestub)
	if(NOT TARGET ${_lib})
		message(STATUS "DiskUsage: skipped, needs ${_lib}")
		return()
	endif()
endforeach()

Application(
	DiskUsage

	SOURCES
	App.cpp
	ControlsView.cpp
	DiskUsage.cpp
	InfoWindow.cpp
	MainWindow.cpp
	PieView.cpp
	Scanner.cpp
	Snapshot.cpp
	StatusView.cpp
	VolumeView.cpp

	LIBS shared localestub
	RDEF DiskUsage.rdef
)

UsePrivateHeaders(DiskUsage mount shared tracker storage)
target_include_directories(DiskUsage PRIVATE ${PROJECT_SOURCE_DIR}/src/kits/tracker)
DoCatalogs("x-vnd.vos-DiskUsage" apps/diskusage)
