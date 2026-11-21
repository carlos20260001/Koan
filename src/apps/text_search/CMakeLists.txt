# Copyright 2026, Koan contributors. All rights reserved.
# Distributed under the terms of the MIT License.

foreach(_lib tracker textencoding shared localestub)
	if(NOT TARGET ${_lib})
		message(STATUS "TextSearch: skipped, needs ${_lib}")
		return()
	endif()
endforeach()

Application(
	TextSearch

	SOURCES
	ChangesIterator.cpp
	FileIterator.cpp
	InitialIterator.cpp
	GrepApp.cpp
	GrepListView.cpp
	Grepper.cpp
	GrepWindow.cpp
	Model.cpp
	TextSearch.cpp

	LIBS tracker textencoding shared localestub
	RDEF TextSearch.rdef
)

UsePrivateHeaders(TextSearch shared storage)
DoCatalogs("x-vnd.vos.TextSearch" apps/text_search)
