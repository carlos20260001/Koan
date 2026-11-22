# Copyright 2026, Koan contributors. All rights reserved.
# Distributed under the terms of the MIT License.

# Prints the device tree. The device manager wrapper is the Devices app's
# sysfs/libudev one (dm_wrapper_linux.cpp), which keeps the node and
# attribute names of dm_wrapper.c; dm_wrapper.c itself needs the device
# manager syscall of the old kernel. The PCI/USB name tables are generated
# from the .ids files, as for Devices.
find_program(VOS_AWK NAMES gawk mawk awk)
find_library(VOS_DEVICES_UDEV_LIBRARY udev)
find_path(VOS_DEVICES_UDEV_INCLUDE libudev.h)
if(NOT VOS_AWK OR NOT VOS_DEVICES_UDEV_LIBRARY OR NOT VOS_DEVICES_UDEV_INCLUDE)
	message(STATUS "listdev: skipped, needs awk and libudev")
	return()
endif()

set(_devices ${PROJECT_SOURCE_DIR}/src/apps/devices)
set(_ids ${PROJECT_SOURCE_DIR}/src/data/ids)
set(_gen ${CMAKE_CURRENT_BINARY_DIR}/generated)
foreach(_bus pci usb)
	add_custom_command(
		OUTPUT ${_gen}/${_bus}hdr.h
		COMMAND ${CMAKE_COMMAND} -E make_directory ${_gen}
		COMMAND ${VOS_AWK} -v HEADERFILE=${_gen}/${_bus}hdr.h
			-f ${_devices}/${_bus}-header.awk ${_ids}/${_bus}.ids
		DEPENDS ${_devices}/${_bus}-header.awk ${_ids}/${_bus}.ids
		VERBATIM
	)
endforeach()

Application(listdev
	SOURCES
	listdev.cpp
	${_devices}/dm_wrapper_linux.cpp
	${_gen}/pcihdr.h
	${_gen}/usbhdr.h

	LIBS ${VOS_DEVICES_UDEV_LIBRARY}
	INCLUDES ${_gen} ${VOS_DEVICES_UDEV_INCLUDE}
)
UsePrivateHeaders(listdev shared system)
