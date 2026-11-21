# Copyright 2026, Koan contributors. All rights reserved.
# Distributed under the terms of the MIT License.

TrackerAddOn(
	OpenTerminal SHARED

	SOURCES
	OpenTerminal.cpp

	LIBS be tracker

	RDEF OpenTerminal.rdef
)

set_target_properties(OpenTerminal PROPERTIES OUTPUT_NAME "Open Terminal")
UsePrivateHeaders(OpenTerminal tracker)
