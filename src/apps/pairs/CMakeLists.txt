# Copyright 2026, Koan contributors. All rights reserved.
# Distributed under the terms of the MIT License.

if(NOT TARGET localestub)
	message(STATUS "Pairs: skipped, needs localestub")
	return()
endif()

Application(
	Pairs

	SOURCES
	Pairs.cpp
	PairsButton.cpp
	PairsView.cpp
	PairsWindow.cpp

	LIBS localestub
	RDEF Pairs.rdef
)

DoCatalogs("x-vnd.vos-Pairs" apps/pairs)
