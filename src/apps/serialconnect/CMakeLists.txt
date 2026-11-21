# Copyright 2026, Koan contributors. All rights reserved.
# Distributed under the terms of the MIT License.

# BSerialPort is the game kit's termios implementation (libgame); the
# terminal emulation is the bundled libvterm.
foreach(_lib game tracker localestub)
	if(NOT TARGET ${_lib})
		message(STATUS "SerialConnect: skipped, needs ${_lib}")
		return()
	endif()
endforeach()

Application(
	SerialConnect

	SOURCES
	CustomRateWindow.cpp
	FileSender.cpp
	SerialApp.cpp
	SerialWindow.cpp
	TermView.cpp
	XModem.cpp
	libvterm/src/encoding.c
	libvterm/src/input.c
	libvterm/src/parser.c
	libvterm/src/pen.c
	libvterm/src/screen.c
	libvterm/src/state.c
	libvterm/src/unicode.c
	libvterm/src/vterm.c

	LIBS game tracker localestub
	RDEF SerialConnect.rdef
)

UsePrivateHeaders(SerialConnect interface)
target_include_directories(SerialConnect PRIVATE
	${CMAKE_CURRENT_SOURCE_DIR}/libvterm/include
)

DoCatalogs("x-vnd.vos.SerialConnect" apps/serialconnect)
