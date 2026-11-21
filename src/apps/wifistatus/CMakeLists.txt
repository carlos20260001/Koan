# Copyright 2026, Koan contributors. All rights reserved.
# Distributed under the terms of the MIT License.

# A Deskbar applet and an application in one file (RunnableAddOn). The
# Wi-Fi state machine is shared with the WiFi preferences, whose radio
# details come from Linux's wireless extensions (LinuxWireless.cpp).
foreach(_lib shared localestub)
	if(NOT TARGET ${_lib})
		message(STATUS "WiFiStatus: skipped, needs ${_lib}")
		return()
	endif()
endforeach()

set(_wifi_prefs ${PROJECT_SOURCE_DIR}/src/preferences/wifi)

RunnableAddOn(
	WiFiStatus

	SOURCES
	WiFiStatus.cpp
	${_wifi_prefs}/WiFiController.cpp
	${_wifi_prefs}/LinuxWireless.cpp

	LIBS shared localestub
	RDEF WiFiStatus.rdef
)

UsePrivateHeaders(WiFiStatus interface net shared)
target_include_directories(WiFiStatus PRIVATE
	${_wifi_prefs}
	${PROJECT_SOURCE_DIR}/src/libs/compat)
DoCatalogs("x-vnd.vos-WiFiStatus" apps/wifistatus)
