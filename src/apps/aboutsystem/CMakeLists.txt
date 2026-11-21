# Copyright 2026, Koan contributors. All rights reserved.
# Distributed under the terms of the MIT License.

foreach(_lib localestub)
	if(NOT TARGET ${_lib})
		message(STATUS "AboutSystem: skipped, needs ${_lib}")
		return()
	endif()
endforeach()

Application(
	AboutSystem

	SOURCES
	AboutSystem.cpp
	HyperTextActions.cpp
	HyperTextView.cpp
	Utilities.cpp

	LIBS localestub
	RDEF AboutSystem.rdef
)

UsePrivateHeaders(AboutSystem app interface libroot shared system kernel)
DoCatalogs("x-vnd.vos-About" apps/aboutsystem)
