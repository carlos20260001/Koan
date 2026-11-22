# Copyright 2026, Dario Casalinuovo. All rights reserved.
# Distributed under the terms of the MIT License.

ScreenSaverAddOn(spider
	SOURCES
	Polygon.cpp PolygonQueue.cpp SpiderSaver.cpp SpiderView.cpp

	LIBS be localestub
	RDEF Spider.rdef
)
DoCatalogs("x-vnd.vos-SpiderScreensaver" add-ons/screen_savers/spider)
