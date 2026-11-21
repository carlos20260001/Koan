# Copyright 2026, Koan contributors. All rights reserved.
# Distributed under the terms of the MIT License.

foreach(_lib shared localestub)
	if(NOT TARGET ${_lib})
		message(STATUS "CharacterMap: skipped, needs ${_lib}")
		return()
	endif()
endforeach()

Application(
	CharacterMap

	SOURCES
	CharacterMap.cpp
	CharacterView.cpp
	CharacterWindow.cpp
	UnicodeBlocks.cpp
	UnicodeBlockView.cpp

	LIBS localestub
	RDEF CharacterMap.rdef
)

UsePrivateHeaders(CharacterMap shared system)
DoCatalogs("x-vnd.vos-CharacterMap" apps/charactermap)
