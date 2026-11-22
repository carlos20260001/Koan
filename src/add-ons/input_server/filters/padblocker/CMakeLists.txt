# Copyright 2026, Koan contributors.
# Distributed under the terms of the MIT License.

# Drops mouse-downs that follow a key-up within the Input preference's
# "Keyboard lock delay" (padblocker_threshold in the touchpad settings).
AddOn(
	padblocker
	SHARED

	LIBS
	be

	SOURCES
	PadBlocker.cpp

	RDEF
	PadBlocker.rdef
)

UsePrivateHeaders(padblocker app input)
