# Copyright 2026, Koan contributors. All rights reserved.
# Distributed under the terms of the MIT License.

# Opens Tracker's preferences window. The target is Tracker_prefs because
# the Tracker application already uses the name; the file is
# /system/preferences/Tracker (build/profiles/preferences.cmake).
Application(
	Tracker_prefs

	SOURCES
	TrackerPreferences.cpp

	LIBS localestub
	RDEF TrackerPreferences.rdef
)

set_target_properties(Tracker_prefs PROPERTIES OUTPUT_NAME Tracker)
DoCatalogs("x-vnd.vos-TrackerPreferences" preferences/tracker)
