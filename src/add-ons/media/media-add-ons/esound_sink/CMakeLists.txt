# Copyright 2026, Koan contributors.
# Distributed under the terms of the MIT License.

# Plays to an EsounD server over TCP (BSD sockets, in libbe's network kit
# on this tree).
MediaKitAddOn(esound_sink.media_addon
	SOURCES
	ESDEndpoint.cpp
	ESDSinkAddOn.cpp
	ESDSinkNode.cpp
)
