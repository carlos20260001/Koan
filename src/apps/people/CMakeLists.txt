# Copyright 2026, Koan contributors. All rights reserved.
# Distributed under the terms of the MIT License.

foreach(_lib tracker translation shared localestub)
	if(NOT TARGET ${_lib})
		message(STATUS "People: skipped, needs ${_lib}")
		return()
	endif()
endforeach()

Application(
	People

	SOURCES
	main.cpp
	AttributeTextControl.cpp
	PeopleApp.cpp
	PersonView.cpp
	PersonWindow.cpp
	PictureView.cpp

	LIBS tracker translation localestub
	RDEF People.rdef
)

UsePrivateHeaders(People shared)
DoCatalogs("x-vnd.Be-PEPL" apps/people)
