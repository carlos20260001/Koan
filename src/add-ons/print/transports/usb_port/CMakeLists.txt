# Copyright 2026, Koan contributors.
# Distributed under the terms of the MIT License.

PrintAddOn(print_usb "USB Port"
	SOURCES
	USBTransport.cpp
	${PRINT_TRANSPORT_ADDON_SOURCE}
)
UsePrivateHeaders(print_usb print)
