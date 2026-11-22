# Copyright 2026, Koan contributors.
# Distributed under the terms of the MIT License.

# libprint.a: the printer driver framework (job and page setup dialogs,
# halftoning, graphics driver, transport glue) linked into the printer
# driver add-ons in src/add-ons/print/drivers and the LPR transport.
AddOn(
	libprint
	STATIC

	SOURCES
	AboutBox.cpp
	AddPrinterDlg.cpp
	BlockingWindow.cpp
	DbgMsg.cpp
	DialogWindow.cpp
	GraphicsDriver.cpp
	Halftone.cpp
	HalftoneView.cpp
	JSDSlider.cpp
	JobData.cpp
	JobSetupDlg.cpp
	MarginView.cpp
	PackBits.cpp
	PageSetupDlg.cpp
	PagesView.cpp
	Preview.cpp
	PrinterData.cpp
	PrinterDriver.cpp
	PrinterCap.cpp
	PrintProcess.cpp
	PrintJobReader.cpp
	PrintUtils.cpp
	SpoolMetaData.cpp
	StatusWindow.cpp
	Transport.cpp
	UIDriver.cpp
	ValidRect.cpp

	INCLUDES
	${PROJECT_SOURCE_DIR}/headers/libs/print/libprint
)

# BEFORE: the global include path has src/kits/tracker, whose
# StatusWindow.h would shadow libprint's.
target_include_directories(libprint BEFORE PRIVATE
	${PROJECT_SOURCE_DIR}/headers/libs/print/libprint)
set_target_properties(libprint PROPERTIES OUTPUT_NAME print)
target_link_libraries(libprint PRIVATE be)
UsePrivateHeaders(libprint shared print)
