# Copyright 2026, Koan contributors. All rights reserved.
# Distributed under the terms of the MIT License.

foreach(_lib tracker shared localestub)
	if(NOT TARGET ${_lib})
		message(STATUS "Expander: skipped, needs ${_lib}")
		return()
	endif()
endforeach()

Application(
	Expander

	SOURCES
	ExpanderApp.cpp
	ExpanderWindow.cpp
	ExpanderThread.cpp
	GenericThread.cpp
	ExpanderSettings.cpp
	ExpanderPreferences.cpp
	DirectoryFilePanel.cpp
	ExpanderRules.cpp
	PasswordAlert.cpp

	LIBS tracker shared localestub
	RDEF Expander.rdef
)

UsePrivateHeaders(Expander shared)
DoCatalogs("x-vnd.vos-Expander" apps/expander)
