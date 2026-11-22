# Copyright 2026, Koan contributors. All rights reserved.
# Distributed under the terms of the MIT License.

# Test client of the classic Media Kit (libmedia.so).
Application(media_client
	SOURCES
	media_client.cpp
	MediaPlay.cpp
	MediaRecord.cpp
	MediaTest.cpp

	LIBS media
	INCLUDES
	${PROJECT_SOURCE_DIR}/headers/private/media
	${PROJECT_SOURCE_DIR}/headers/private/media/experimental
)
UseMediaKit(media_client)
