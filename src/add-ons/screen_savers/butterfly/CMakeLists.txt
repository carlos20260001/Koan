# Copyright 2026, Dario Casalinuovo. All rights reserved.
# Distributed under the terms of the MIT License.

ScreenSaverAddOn(butterfly
	SOURCES
	Butterfly.cpp

	LIBS be localestub
	RDEF Butterfly.rdef
)
DoCatalogs("x-vnd.vos-ButterflyScreensaver" add-ons/screen_savers/butterfly)
