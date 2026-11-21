# Copyright 2026, Koan contributors. All rights reserved.
# Distributed under the terms of the MIT License.

# A Deskbar applet and an application in one file (RunnableAddOn). The
# network kit in libbe keeps air/OS's API on NetworkManager.
foreach(_lib shared localestub)
	if(NOT TARGET ${_lib})
		message(STATUS "NetworkStatus: skipped, needs ${_lib}")
		return()
	endif()
endforeach()

RunnableAddOn(
	NetworkStatus

	SOURCES
	NetworkStatus.cpp
	NetworkStatusView.cpp
	NetworkStatusWindow.cpp
	RadioView.cpp
	WirelessNetworkMenuItem.cpp

	LIBS shared localestub
	RDEF NetworkStatus.rdef NetworkStatusIcons.rdef
)

UsePrivateHeaders(NetworkStatus interface net)
DoCatalogs("x-vnd.vos-NetworkStatus" apps/networkstatus)
