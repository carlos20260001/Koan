# Copyright 2026, Koan contributors. All rights reserved.
# Distributed under the terms of the MIT License.

foreach(_lib game tracker translation shared localestub)
	if(NOT TARGET ${_lib})
		message(STATUS "Magnify: skipped, needs ${_lib}")
		return()
	endif()
endforeach()

Application(
	Magnify

	SOURCES
	Magnify.cpp

	LIBS game tracker translation localestub
	RDEF Magnify.rdef
)

UsePrivateHeaders(Magnify shared)
DoCatalogs("x-vnd.vos-Magnify" apps/magnify)
