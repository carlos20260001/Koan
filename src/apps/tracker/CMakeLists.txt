# Copyright 2026, Koan contributors. All rights reserved.
# Distributed under the terms of the MIT License.

# The Tracker application: main() and its resources. Everything else is
# libtracker (src/kits/tracker). janus_session starts it by its signature,
# application/x-vnd.Be-TRAK (Tracker.rdef).
Application(
	Tracker

	SOURCES
	main.cpp

	LIBS tracker localestub

	RDEF Tracker.rdef
)

UsePrivateHeaders(Tracker tracker shared support)
target_include_directories(Tracker PRIVATE ${CMAKE_SOURCE_DIR}/src/kits/tracker)
DoCatalogs("x-vnd.Be-TRAK" apps/tracker)
