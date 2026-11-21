# Copyright 2026, Koan contributors. All rights reserved.
# Distributed under the terms of the MIT License.

foreach(_lib tracker translation)
	if(NOT TARGET ${_lib})
		message(STATUS "ResEdit: skipped, needs ${_lib}")
		return()
	endif()
endforeach()

# BColumnListView is part of libbe here (libcolumnlistview.a in Jam).
Application(
	ResEdit

	SOURCES
	App.cpp
	BitmapView.cpp
	ImageEditor.cpp
	InlineEditor.cpp
	MiscEditors.cpp
	NumberEditors.cpp
	PreviewColumn.cpp
	ResFields.cpp
	ResListView.cpp
	ResourceData.cpp
	ResourceRoster.cpp
	ResView.cpp
	ResWindow.cpp

	LIBS tracker translation
)

UsePrivateHeaders(ResEdit shared interface)
