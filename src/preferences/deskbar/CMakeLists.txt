# Copyright 2026, Koan contributors. All rights reserved.
# Distributed under the terms of the MIT License.

# Opens the Deskbar's preferences window. The target is Deskbar_prefs
# because the Deskbar application already uses the name; the file is
# /system/preferences/Deskbar (build/profiles/preferences.cmake).
Application(
	Deskbar_prefs

	SOURCES
	DeskbarPreferences.cpp

	LIBS localestub
	RDEF DeskbarPreferences.rdef
)

set_target_properties(Deskbar_prefs PROPERTIES OUTPUT_NAME Deskbar)
target_include_directories(Deskbar_prefs PRIVATE
	${PROJECT_SOURCE_DIR}/src/apps/deskbar)
DoCatalogs("x-vnd.vos-DeskbarPreferences" preferences/deskbar)
