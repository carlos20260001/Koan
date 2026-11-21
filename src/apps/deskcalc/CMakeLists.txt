# Copyright 2026, Koan contributors. All rights reserved.
# Distributed under the terms of the MIT License.

foreach(_lib expression_parser mapm shared localestub)
	if(NOT TARGET ${_lib})
		message(STATUS "DeskCalc: skipped, needs ${_lib}")
		return()
	endif()
endforeach()

Application(
	DeskCalc

	SOURCES
	CalcApplication.cpp
	CalcOptions.cpp
	CalcView.cpp
	CalcWindow.cpp
	DeskCalc.cpp
	ExpressionTextView.cpp
	InputTextView.cpp

	LIBS localestub expression_parser mapm shared
	RDEF DeskCalc.rdef
)

UsePrivateHeaders(DeskCalc interface shared)
DoCatalogs("x-vnd.vos-DeskCalc" apps/deskcalc)
