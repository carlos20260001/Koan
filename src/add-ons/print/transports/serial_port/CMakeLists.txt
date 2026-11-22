# Copyright 2026, Koan contributors.
# Distributed under the terms of the MIT License.

PrintAddOn(print_serial "Serial Port"
	SOURCES
	SerialTransport.cpp
	${PRINT_TRANSPORT_ADDON_SOURCE}
)
UsePrivateHeaders(print_serial print)
