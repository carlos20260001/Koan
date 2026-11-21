# Copyright 2026, Koan contributors. All rights reserved.
# Distributed under the terms of the MIT License.

# The client of app_server's remote HWInterface: it shares the wire
# protocol sources with src/servers/app/drawing/interface/remote.
set(_remote "${PROJECT_SOURCE_DIR}/src/servers/app/drawing/interface/remote")

Application(
	RemoteDesktop

	SOURCES
	RemoteDesktop.cpp
	RemoteView.cpp
	${_remote}/RemoteMessage.cpp
	${_remote}/NetReceiver.cpp
	${_remote}/NetSender.cpp
	${_remote}/StreamingRingBuffer.cpp

	INCLUDES "${_remote}"
	RDEF RemoteDesktop.rdef
)

target_compile_definitions(RemoteDesktop PRIVATE CLIENT_COMPILE)
UsePrivateHeaders(RemoteDesktop interface shared)
