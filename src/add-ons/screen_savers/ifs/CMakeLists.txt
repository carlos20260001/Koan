# Copyright 2026, Dario Casalinuovo. All rights reserved.
# Distributed under the terms of the MIT License.

ScreenSaverAddOn(ifs
	SOURCES
	IFS.cpp IFSSaver.cpp

	LIBS be localestub
	RDEF IFS.rdef
)
DoCatalogs("x-vnd.vos-IFSScreensaver" add-ons/screen_savers/ifs)
