# ported subset: the plain build container has no libbluetooth-dev (the
# bake chroot installs it from build/deps.cmake). Without it the kit is left
# out of be, with a warning, so configure stays usable; upstream requires it.
find_package(PkgConfig)
if(PKG_CONFIG_FOUND)
	pkg_check_modules(BLUEZ bluez)
endif()
if(NOT BLUEZ_FOUND)
	message(WARNING "bluetooth kit: bluez not found (libbluetooth-dev); the "
		"bluetooth kit is left out of be")
	return()
endif()

target_sources(be PRIVATE
	libbluetooth/BlueZBackend.cpp
	libbluetooth/DeviceClass.cpp
	libbluetooth/LocalDevice.cpp
	libbluetooth/ObexClient.cpp
	libbluetooth/RemoteDevice.cpp
	libbluetooth/DiscoveryAgent.cpp
	libbluetooth/DiscoveryListener.cpp

	# air/OS: kept with their API. What reached Haiku's HCI/L2CAP stack
	# through bluetooth_server or L2CAP sockets answers B_NOT_SUPPORTED
	# until it has a BlueZ counterpart (TODO(linux backend) in the sources):
	# LE attribute client, LE pairing (SMP), A2DP source, HID-over-GATT
	# decoding. The SBC encoder, bond store, settings and windows are plain
	# userland.
	A2dpSource.cpp
	AudioSinkSetting.cpp
	CommandManager.cpp
	KitSupport.cpp
	LEAttributeClient.cpp
	LEBondStore.cpp
	LEDeviceStatus.cpp
	LEHIDMouseDecoder.cpp
	LEHIDService.cpp
	LELegacyPairingClient.cpp
	LELegacyPairingCrypto.cpp
	LELog.cpp
	LEPairingSession.cpp
	SbcEncoder.cpp
	UI/BluetoothIconView.cpp
	UI/ConnectionIncoming.cpp
	UI/ConnectionView.cpp
	UI/PincodeWindow.cpp
)

target_include_directories(be PRIVATE
	${CMAKE_CURRENT_SOURCE_DIR}/libbluetooth
	${CMAKE_SOURCE_DIR}/headers/private/bluetooth
)

# Find BlueZ libraries
if(PKG_CONFIG_FOUND)
	if(BLUEZ_FOUND)
		target_include_directories(be PRIVATE ${BLUEZ_INCLUDE_DIRS})
		target_link_libraries(be PRIVATE ${BLUEZ_LIBRARIES})
		target_compile_options(be PRIVATE ${BLUEZ_CFLAGS_OTHER})
	endif()

	# gio/glib (BlueZBackend.cpp's D-Bus transport) is declared once in
	# src/kits/CMakeLists.txt, shared with the network kit's libnm.
endif()
# UI pieces shared by the Bluetooth preflet, BluetoothStatus and the
# SendToBluetooth Tracker add-on: OBEX send/receive windows and settings.
AddOn(
	bluetoothui
	STATIC
	SOURCES
	private/BluetoothSettings.cpp
	private/ObexReceiveAgent.cpp
	private/ObexReceivePrompt.cpp
	private/ObexSendChooser.cpp
	private/ObexTransferWindow.cpp
)

target_include_directories(bluetoothui PRIVATE
	${CMAKE_SOURCE_DIR}/headers/private/bluetooth
)
UsePrivateHeaders(bluetoothui shared)
