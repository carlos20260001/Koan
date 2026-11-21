# Copyright 2026, Koan contributors. All rights reserved.
# Distributed under the terms of the MIT License.

# A Deskbar applet and an application in one file (RunnableAddOn). The
# Bluetooth kit (air/OS's API on BlueZ) is part of libbe, so the kit pieces
# the Jamfile compiled in (AudioSinkSetting, LEBondStore, LEDeviceStatus)
# come from there.
if(NOT TARGET localestub)
	message(STATUS "BluetoothStatus: skipped, needs localestub")
	return()
endif()

RunnableAddOn(
	BluetoothStatus

	SOURCES
	BluetoothStatus.cpp

	LIBS localestub
	RDEF BluetoothStatus.rdef
)

UsePrivateHeaders(BluetoothStatus app)
# The kit's private headers (bluetoothserver_p.h, LEBondStore.h, ...).
target_include_directories(BluetoothStatus PRIVATE
	${PROJECT_SOURCE_DIR}/headers/private/bluetooth)
DoCatalogs("x-vnd.vos-BluetoothStatus" apps/bluetoothstatus)
