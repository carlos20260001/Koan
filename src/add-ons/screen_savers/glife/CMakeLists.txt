# Copyright 2026, Dario Casalinuovo. All rights reserved.
# Distributed under the terms of the MIT License.

ScreenSaverAddOn(glife
	SOURCES
	GLifeConfig.cpp GLifeGrid.cpp GLifeSaver.cpp GLifeView.cpp

	LIBS be localestub ${SCREEN_SAVER_GL_LIBS}
	RDEF GLife.rdef
)
DoCatalogs("x-vnd.vos-GLifeScreensaver" add-ons/screen_savers/glife)
