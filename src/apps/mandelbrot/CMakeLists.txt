# Copyright 2026, Koan contributors. All rights reserved.
# Distributed under the terms of the MIT License.

foreach(_lib localestub tracker translation)
	if(NOT TARGET ${_lib})
		message(STATUS "Mandelbrot: skipped, needs ${_lib}")
		return()
	endif()
endforeach()

Application(
	Mandelbrot

	SOURCES
	Mandelbrot.cpp
	FractalEngine.cpp

	LIBS localestub tracker translation
	RDEF Mandelbrot.rdef
)

UsePrivateHeaders(Mandelbrot interface shared system)
DoCatalogs("x-vnd.vos-Mandelbrot" apps/mandelbrot)
