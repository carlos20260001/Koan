# bluetooth_server: air/OS's Bluetooth server. On Linux BlueZ owns the
# radios, SDP and HID, and the kit talks to it directly; the server is
# BlueZ's pairing agent (BlueZAgent.cpp, org.bluez.Agent1 over sd-bus). The
# Haiku HCI, SDP and HID host code still builds but is not started
# (TODO(linux backend), see BluetoothServer::ReadyToRun()).
Server(
	bluetooth_server

	SOURCES
	BluetoothServer.cpp
	ControllerFirmware.cpp
	DeviceManager.cpp
	HCIControllerAccessor.cpp
	HCITransportAccessor.cpp
	HIDHost.cpp
	LocalDeviceHandler.cpp
	LocalDeviceImpl.cpp
	LocalDeviceLE.cpp
	SDPServer.cpp
	SDPClient.cpp
	BlueZAgent.cpp

	LIBS systemd
	RDEF bluetooth_server.rdef
)

VosPrivateHeaders(bluetooth_server shared bluetooth net)
