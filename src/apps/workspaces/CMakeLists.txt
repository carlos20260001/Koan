# Copyright 2026, Koan contributors. All rights reserved.
# Distributed under the terms of the MIT License.

# A window and a Deskbar applet in one file: Deskbar loads it with
# load_add_on() and calls instantiate_deskbar_item().
foreach(_lib shared localestub)
	if(NOT TARGET ${_lib})
		message(STATUS "Workspaces: skipped, needs ${_lib}")
		return()
	endif()
endforeach()

RunnableAddOn(
	Workspaces

	SOURCES
	Workspaces.cpp

	LIBS shared localestub
	RDEF Workspaces.rdef
)

UsePrivateHeaders(Workspaces app interface shared)
DoCatalogs("x-vnd.Be-WORK" apps/workspaces)
