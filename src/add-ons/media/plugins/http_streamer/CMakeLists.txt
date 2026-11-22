# Copyright 2026, Koan contributors.
# Distributed under the terms of the MIT License.

# BHttpRequest and BUrlProtocolRoster (libnetservices) are part of libbe on
# this tree (src/kits/network/CMakeLists.txt).
MediaKitAddOn(http_streamer
	SOURCES
	HTTPStreamerPlugin.cpp
	HTTPMediaIO.cpp
)
UsePrivateHeaders(http_streamer net)
target_include_directories(http_streamer PRIVATE
	${PROJECT_SOURCE_DIR}/headers/private/netservices)
