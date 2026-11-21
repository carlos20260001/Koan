# Copyright 2026, Koan contributors. All rights reserved.
# Distributed under the terms of the MIT License.

# A Deskbar applet and an application in one file (RunnableAddOn). On Linux
# the batteries come from UPower over the system D-Bus (sd-bus, as the
# power_daemon uses it); Haiku's ACPI and APM interfaces stay as fallbacks
# and find nothing.
foreach(_lib shared translation localestub)
	if(NOT TARGET ${_lib})
		message(STATUS "PowerStatus: skipped, needs ${_lib}")
		return()
	endif()
endforeach()

RunnableAddOn(
	PowerStatus

	SOURCES
	ACPIDriverInterface.cpp
	APMDriverInterface.cpp
	DriverInterface.cpp
	ExtendedInfoWindow.cpp
	PowerStatus.cpp
	PowerStatusView.cpp
	PowerStatusWindow.cpp
	UPowerDriverInterface.cpp

	LIBS shared translation localestub systemd
	RDEF PowerStatus.rdef
)

UsePrivateHeaders(PowerStatus interface shared system)
DoCatalogs("x-vnd.vos-PowerStatus" apps/powerstatus)
