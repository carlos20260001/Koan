# Copyright 2026, Koan contributors.
# Distributed under the terms of the MIT License.

# Pen input method. compat/ carries a minimal BStringIO, which the kits do
# not provide.
AddOn(
	pen
	SHARED

	LIBS
	be textencoding

	SOURCES
	PenInputServerMethod.cpp
	PenInputLooper.cpp
	PenInputInkWindow.cpp
	PenInputBackend.cpp
	DumpMessage.cpp
	compat/StringIO.cpp

	INCLUDES
	${CMAKE_CURRENT_SOURCE_DIR}/compat
)

UsePrivateHeaders(pen interface)
