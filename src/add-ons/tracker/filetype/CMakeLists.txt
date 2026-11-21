# Copyright 2026, Koan contributors. All rights reserved.
# Distributed under the terms of the MIT License.

TrackerAddOn(
	FileType SHARED

	SOURCES
	FileType.cpp

	LIBS be tracker

	RDEF FileType.rdef
)

UsePrivateHeaders(FileType tracker)
