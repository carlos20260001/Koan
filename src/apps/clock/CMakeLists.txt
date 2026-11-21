# Copyright 2026, Koan contributors. All rights reserved.
# Distributed under the terms of the MIT License.

if(NOT TARGET localestub)
	message(STATUS "Clock: skipped, needs localestub")
	return()
endif()

Application(
	Clock

	SOURCES
	cl_view.cpp
	cl_wind.cpp
	clock.cpp

	LIBS localestub
	RDEF Clock.rdef
)

DoCatalogs("x-vnd.vos-Clock" apps/clock)
