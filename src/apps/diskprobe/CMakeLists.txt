# Copyright 2026, Koan contributors. All rights reserved.
# Distributed under the terms of the MIT License.

foreach(_lib tracker translation expression_parser mapm localestub)
	if(NOT TARGET ${_lib})
		message(STATUS "DiskProbe: skipped, needs ${_lib}")
		return()
	endif()
endforeach()

Application(
	DiskProbe

	SOURCES
	DiskProbe.cpp
	DataEditor.cpp
	DataView.cpp
	ProbeWindow.cpp
	FileWindow.cpp
	AttributeWindow.cpp
	TypeEditors.cpp
	ProbeView.cpp
	OpenWindow.cpp
	FindWindow.cpp

	LIBS localestub tracker translation expression_parser mapm
	RDEF DiskProbe.rdef
)

UsePrivateHeaders(DiskProbe interface shared)
DoCatalogs("x-vnd.vos-DiskProbe" apps/diskprobe)
