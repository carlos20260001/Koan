# Copyright 2026, Koan contributors. All rights reserved.
# Distributed under the terms of the MIT License.
#
# MusicCollection: lists audio files found by a BQuery and kept current by
# node monitoring. On Linux the storage kit answers both: queries through
# its index back end, node monitoring through Nexus' node monitor.
# NodeMonitorHandler, compiled into the app on Haiku, is part of libbe here.

foreach(_lib shared localestub alm)
	if(NOT TARGET ${_lib})
		message(STATUS "MusicCollection: skipped, needs ${_lib}")
		return()
	endif()
endforeach()

Application(
	MusicCollection

	SOURCES
	FileMonitor.cpp
	main.cpp
	MusicCollectionWindow.cpp
	QueryMonitor.cpp

	LIBS localestub shared alm
	RDEF MusicCollection.rdef
)

UsePrivateHeaders(MusicCollection storage shared)
target_include_directories(MusicCollection PRIVATE
	${PROJECT_SOURCE_DIR}/headers/libs/alm
	${PROJECT_SOURCE_DIR}/headers/libs/linprog
)
DoCatalogs("x-vnd.MusicCollection" apps/musiccollection)
