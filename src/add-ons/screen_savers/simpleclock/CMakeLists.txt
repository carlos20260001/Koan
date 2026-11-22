# Copyright 2026, Dario Casalinuovo. All rights reserved.
# Distributed under the terms of the MIT License.

ScreenSaverAddOn(simpleclock
	SOURCES
	SimpleClock.cpp

	LIBS be localestub
	RDEF SimpleClock.rdef
)
DoCatalogs("x-vnd.vos-SimpleClock" add-ons/screen_savers/simpleclock)
