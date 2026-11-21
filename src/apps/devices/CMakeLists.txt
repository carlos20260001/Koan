# Copyright 2026, Koan contributors. All rights reserved.
# Distributed under the terms of the MIT License.

# Devices reads the hardware tree from sysfs through libudev
# (dm_wrapper_linux.cpp, in place of air/OS's device manager syscalls) and
# USB descriptors through the USB kit in libbe (libusb).
foreach(_lib tracker shared localestub)
	if(NOT TARGET ${_lib})
		message(STATUS "Devices: skipped, needs ${_lib}")
		return()
	endif()
endforeach()
find_library(VOS_DEVICES_UDEV_LIBRARY udev)
find_path(VOS_DEVICES_UDEV_INCLUDE libudev.h)
if(NOT VOS_DEVICES_UDEV_LIBRARY OR NOT VOS_DEVICES_UDEV_INCLUDE)
	message(STATUS "Devices: skipped, needs libudev")
	return()
endif()
find_program(VOS_AWK NAMES gawk mawk awk)
find_package(Python3 COMPONENTS Interpreter)
if(NOT VOS_AWK OR NOT Python3_Interpreter_FOUND)
	message(STATUS "Devices: skipped, needs awk and python3")
	return()
endif()

# The ID tables air/OS's Jamfile generated: PCI and USB names from
# src/data/ids/{pci,usb}.ids, ISA PnP names from isapnp_devids.txt, ACPI/PnP
# vendor names from the UEFI registries.
set(_ids "${PROJECT_SOURCE_DIR}/src/data/ids")
set(_gen "${CMAKE_CURRENT_BINARY_DIR}/generated")
set(_src "${CMAKE_CURRENT_SOURCE_DIR}")
add_custom_command(
	OUTPUT "${_gen}/pcihdr.h"
	COMMAND ${CMAKE_COMMAND} -E make_directory "${_gen}"
	COMMAND "${VOS_AWK}" -v "HEADERFILE=${_gen}/pcihdr.h"
		-f "${_src}/pci-header.awk" "${_ids}/pci.ids"
	DEPENDS "${_src}/pci-header.awk" "${_ids}/pci.ids"
	COMMENT "Generating Devices pcihdr.h"
	VERBATIM
)
add_custom_command(
	OUTPUT "${_gen}/usbhdr.h"
	COMMAND ${CMAKE_COMMAND} -E make_directory "${_gen}"
	COMMAND "${VOS_AWK}" -v "HEADERFILE=${_gen}/usbhdr.h"
		-f "${_src}/usb-header.awk" "${_ids}/usb.ids"
	DEPENDS "${_src}/usb-header.awk" "${_ids}/usb.ids"
	COMMENT "Generating Devices usbhdr.h"
	VERBATIM
)
add_custom_command(
	OUTPUT "${_gen}/isapnpids.h"
	COMMAND ${CMAKE_COMMAND} -E make_directory "${_gen}"
	COMMAND sh -c "grep '^PNP[[:xdigit:]][[:xdigit:]][[:xdigit:]][[:xdigit:]]' \"$1\" | \"$2\" -f \"$3\" > \"$4\""
		sh "${_src}/isapnp_devids.txt" "${VOS_AWK}" "${_src}/devlist2h.awk"
		"${_gen}/isapnpids.h"
	DEPENDS "${_src}/isapnp_devids.txt" "${_src}/devlist2h.awk"
	COMMENT "Generating Devices isapnpids.h"
	VERBATIM
)
add_custom_command(
	OUTPUT "${_gen}/acpipnpids.h"
	COMMAND ${CMAKE_COMMAND} -E make_directory "${_gen}"
	COMMAND sh -c "\"$1\" \"$2\" \"$3\" \"$4\" > \"$5\" 2>/dev/null"
		sh "${Python3_EXECUTABLE}" "${_src}/acpipnplist2h.py"
		"${_ids}/acpi_id_registry.csv" "${_ids}/pnp_id_registry.csv"
		"${_gen}/acpipnpids.h"
	DEPENDS "${_src}/acpipnplist2h.py" "${_ids}/acpi_id_registry.csv"
		"${_ids}/pnp_id_registry.csv"
	COMMENT "Generating Devices acpipnpids.h"
	VERBATIM
)

Application(
	Devices

	SOURCES
	DevicesApplication.cpp
	DevicesView.cpp
	DriverUtils.cpp
	dm_wrapper_linux.cpp
	DeviceACPI.cpp
	DevicePCI.cpp
	DeviceSCSI.cpp
	DeviceUSB.cpp
	Device.cpp
	PropertyList.cpp
	"${_gen}/pcihdr.h"
	"${_gen}/usbhdr.h"
	"${_gen}/isapnpids.h"
	"${_gen}/acpipnpids.h"

	LIBS tracker shared localestub ${VOS_DEVICES_UDEV_LIBRARY}
	RDEF Devices.rdef
)

target_include_directories(Devices PRIVATE "${_gen}" ${VOS_DEVICES_UDEV_INCLUDE})
UsePrivateHeaders(Devices app interface shared system)
DoCatalogs("x-vnd.vos-Devices" apps/devices)
