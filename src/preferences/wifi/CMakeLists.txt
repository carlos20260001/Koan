# Copyright 2026, Koan contributors. All rights reserved.
# Distributed under the terms of the MIT License.

# The network kit (libnetapi in libbe) keeps air/OS's API on NetworkManager.
Application(
	WiFi

	SOURCES
	WiFi.cpp
	WiFiWindow.cpp
	WiFiController.cpp
	NetworkListView.cpp
	JoinOtherWindow.cpp
	LinuxWireless.cpp

	LIBS shared localestub
	RDEF WiFi.rdef
)

UsePrivateHeaders(WiFi interface net shared)
# FreeBSD's if_media.h (802.11 PHY modes), headers only, as air/OS's
# Jamfile used it. The radio details come from Linux's wireless extensions
# (LinuxWireless.cpp).
target_include_directories(WiFi PRIVATE
	${PROJECT_SOURCE_DIR}/src/libs/compat)
DoCatalogs("x-vnd.vos-WiFi" preferences/wifi)
