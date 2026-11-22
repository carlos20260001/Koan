# Copyright 2026, Koan contributors.
# Distributed under the terms of the MIT License.

PrintDriverAddOn(print_ps "PS Compatible"
	SOURCES
	PSEntry.cpp
	PS.cpp
	PSCap.cpp
	PSData.cpp
	SelectPPDDlg.cpp
	PPDParser.cpp
	FilterIO.cpp

	RDEF PS.rdef
)
