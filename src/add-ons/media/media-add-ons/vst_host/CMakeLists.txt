# Copyright 2026, Koan contributors.
# Distributed under the terms of the MIT License.

# Hosts VST 2 plug-ins (shared objects with a VSTPluginMain/main entry) as
# Media Kit filter nodes.
MediaKitAddOn(vst_host.media_addon
	SOURCES
	VSTHost.cpp
	VSTAddOn.cpp
	VSTNode.cpp

	RDEF VSTHost.rdef
)
