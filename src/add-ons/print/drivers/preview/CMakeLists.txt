# Copyright 2026, Koan contributors.
# Distributed under the terms of the MIT License.

PrintDriverAddOn(print_preview "Preview"
	SOURCES
	PageSetupWindow.cpp
	JobSetupWindow.cpp
	Driver.cpp
	PrinterDriver.cpp
	PreviewDriver.cpp

	RDEF Preview.rdef
)
