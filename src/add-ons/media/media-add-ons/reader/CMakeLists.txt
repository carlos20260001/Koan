# Copyright 2026, Koan contributors.
# Distributed under the terms of the MIT License.

MediaKitAddOn(reader.media_addon
	SOURCES
	../AbstractFileInterfaceNode.cpp
	../AbstractFileInterfaceAddOn.cpp
	MediaReader.cpp
	MediaReaderAddOn.cpp
	../misc.cpp

	INCLUDES ${CMAKE_CURRENT_SOURCE_DIR}/..
)
