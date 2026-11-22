# Copyright 2026, Koan contributors.
# Distributed under the terms of the MIT License.

MediaKitAddOn(writer.media_addon
	SOURCES
	../AbstractFileInterfaceNode.cpp
	../AbstractFileInterfaceAddOn.cpp
	MediaWriter.cpp
	MediaWriterAddOn.cpp
	../misc.cpp

	INCLUDES ${CMAKE_CURRENT_SOURCE_DIR}/..
)
