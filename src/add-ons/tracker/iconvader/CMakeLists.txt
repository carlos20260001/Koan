# Copyright 2026, Koan contributors. All rights reserved.
# Distributed under the terms of the MIT License.

TrackerAddOn(
	IconVader SHARED

	SOURCES
	IconVader.cpp

	LIBS be tracker

	RDEF IconVader.rdef
)

# PoseView.h pulls in the private headers libtracker builds with.
UsePrivateHeaders(IconVader tracker app interface shared mount storage system support print)
target_include_directories(IconVader PRIVATE ${CMAKE_SOURCE_DIR}/src/kits/tracker)
