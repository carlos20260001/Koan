# Copyright 2026, Koan contributors. All rights reserved.
# Distributed under the terms of the MIT License.

foreach(_lib localestub tracker)
	if(NOT TARGET ${_lib})
		message(STATUS "Sudoku: skipped, needs ${_lib}")
		return()
	endif()
endforeach()

Application(
	Sudoku

	SOURCES
	ProgressWindow.cpp
	Sudoku.cpp
	SudokuField.cpp
	SudokuGenerator.cpp
	SudokuSolver.cpp
	SudokuView.cpp
	SudokuWindow.cpp

	LIBS localestub tracker
	RDEF Sudoku.rdef
)

UsePrivateHeaders(Sudoku shared)
DoCatalogs("x-vnd.vos-Sudoku" apps/sudoku)
