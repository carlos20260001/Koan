# Copyright 2026, Koan contributors. All rights reserved.
# Distributed under the terms of the MIT License.

# The Bluetooth kit (air/OS's API on BlueZ) is part of libbe.
Application(
	Bluetooth

	SOURCES
	AdvancedWindow.cpp
	BluetoothDeviceView.cpp
	BluetoothMain.cpp
	BluetoothSettings.cpp
	BluetoothSettingsView.cpp
	BluetoothWindow.cpp
	BluetoothWorkers.cpp
	DeviceIcons.cpp
	DeviceListItem.cpp
	DeviceModel.cpp
	ExtendedLocalDeviceView.cpp
	NearbyScanner.cpp

	LIBS shared localestub
	RDEF Bluetooth.rdef
)

UsePrivateHeaders(Bluetooth app shared)
# The kit's private headers (bluetoothserver_p.h, LEBondStore.h, ...);
# UsePrivateHeaders has no "bluetooth" entry.
target_include_directories(Bluetooth PRIVATE
	${PROJECT_SOURCE_DIR}/headers/private/bluetooth)
DoCatalogs("x-vnd.vos-BluetoothPrefs" preferences/bluetooth)
