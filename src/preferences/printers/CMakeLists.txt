# Copyright 2026, Koan contributors. All rights reserved.
# Distributed under the terms of the MIT License.

Application(
	Printers

	SOURCES
	AddPrinterDialog.cpp
	Printers.cpp
	PrintersWindow.cpp
	PrinterListView.cpp
	JobListView.cpp
	ScreenSettings.cpp
	SpoolFolder.cpp
	TestPageView.cpp
	TransportMenu.cpp
	Globals.cpp

	LIBS translation printutils localestub
	RDEF Printers.rdef
)

UsePrivateHeaders(Printers shared print)
DoCatalogs("x-vnd.Be-PRNT" preferences/printers)
