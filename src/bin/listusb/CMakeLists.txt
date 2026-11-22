# Copyright 2026, Koan contributors. All rights reserved.
# Distributed under the terms of the MIT License.

# Lists USB devices through the USB kit (BUSBRoster on libusb/udev, in
# libbe). The vendor/product name table is generated from usb.ids, as for
# the Devices app.
find_program(VOS_AWK NAMES gawk mawk awk)
set(_gen ${CMAKE_CURRENT_BINARY_DIR}/generated)
add_custom_command(
	OUTPUT ${_gen}/usbhdr.h
	COMMAND ${CMAKE_COMMAND} -E make_directory ${_gen}
	COMMAND ${VOS_AWK} -v HEADERFILE=${_gen}/usbhdr.h
		-f ${PROJECT_SOURCE_DIR}/src/apps/devices/usb-header.awk
		${PROJECT_SOURCE_DIR}/src/data/ids/usb.ids
	DEPENDS ${PROJECT_SOURCE_DIR}/src/apps/devices/usb-header.awk
		${PROJECT_SOURCE_DIR}/src/data/ids/usb.ids
	VERBATIM
)

Application(listusb
	SOURCES
	listusb.cpp
	usb_audio.cpp
	usb_cdc.cpp
	usb_hub.cpp
	usb_video.cpp
	${_gen}/usbhdr.h

	INCLUDES
	${_gen}
	${PROJECT_SOURCE_DIR}/headers/private/drivers
)
UsePrivateHeaders(listusb shared)
UseMediaKit(listusb)
