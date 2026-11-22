# Copyright 2026, Koan contributors.
# Distributed under the terms of the MIT License.

PrintAddOn(print_parallel "Parallel Port"
	SOURCES
	ParallelTransport.cpp
	${PRINT_TRANSPORT_ADDON_SOURCE}
)
UsePrivateHeaders(print_parallel print)
