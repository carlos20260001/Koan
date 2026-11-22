# Copyright 2026, Koan contributors.
# Distributed under the terms of the MIT License.

# Raw TCP (port 9100) printing.
PrintAddOn(print_jetdirect "HP JetDirect"
	SOURCES
	print_transport.cpp
	HPJetDirectTransport.cpp
	SetupWindow.cpp

	LIBS localestub
)
