# Copyright 2026, Dario Casalinuovo. All rights reserved.
# Distributed under the terms of the MIT License.

ScreenSaverAddOn(debugnow
	SOURCES
	DebugNow.cpp

	LIBS be localestub
	RDEF DebugNow.rdef
)
DoCatalogs("x-vnd.vos-DebugNowScreensaver" add-ons/screen_savers/debugnow)
