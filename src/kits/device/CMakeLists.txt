# src/kits/device: the USB kit (USBKit.h) lives in libbe, as the legacy
# device kit did. udev enumerates and reports hotplug, libusb-1.0 reads
# descriptors and moves data.
target_sources(be PRIVATE
	USBConfiguration.cpp
	USBDevice.cpp
	USBEndpoint.cpp
	USBInterface.cpp
	USBRoster.cpp
)

# air/OS's other device classes. A2D, D2A and DigitalPort open BeBox
# hardware nodes, AuxDisplay air/OS's auxdisplay driver; on Linux they find
# no such device and report it. JoystickTweaker keeps the Joysticks
# preference's calibration files. BJoystick and BSerialPort are not built
# here: VitruvianOS builds them in the game kit (evdev, termios).
target_sources(be PRIVATE
	A2D.cpp
	AuxDisplay.cpp
	D2A.cpp
	DigitalPort.cpp
	JoystickTweaker.cpp
)
target_include_directories(be PRIVATE ${CMAKE_SOURCE_DIR}/headers/private/device)
