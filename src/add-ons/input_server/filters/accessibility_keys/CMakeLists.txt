# Copyright 2026, Koan contributors.
# Distributed under the terms of the MIT License.

# Screen reader keys: toggles accessibility_server's reader and forwards
# the review keys to it.
AddOn(
	accessibility_keys
	SHARED

	LIBS
	be

	SOURCES
	AccessibilityKeysFilter.cpp
)

UsePrivateHeaders(accessibility_keys interface)
