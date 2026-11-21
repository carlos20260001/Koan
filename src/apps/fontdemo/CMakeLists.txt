# Copyright 2026, Koan contributors. All rights reserved.
# Distributed under the terms of the MIT License.

if(NOT TARGET localestub)
	message(STATUS "FontDemo: skipped, needs localestub")
	return()
endif()

Application(
	FontDemo

	SOURCES
	ControlView.cpp
	FontDemo.cpp
	FontDemoView.cpp

	LIBS localestub
	RDEF FontDemo.rdef
)

DoCatalogs("x-vnd.vos-FontDemo" apps/fontdemo)
