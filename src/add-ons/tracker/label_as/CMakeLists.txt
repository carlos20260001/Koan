# Copyright 2026, Koan contributors. All rights reserved.
# Distributed under the terms of the MIT License.

TrackerAddOn(
	LabelAs SHARED

	SOURCES
	LabelAs.cpp

	LIBS be tracker localestub

	RDEF LabelAs.rdef
)

set_target_properties(LabelAs PROPERTIES OUTPUT_NAME "Label as…")
UsePrivateHeaders(LabelAs mail tracker)
# UsePrivateHeaders has no "mail" entry.
target_include_directories(LabelAs PRIVATE ${CMAKE_SOURCE_DIR}/headers/private/mail)
DoCatalogs("x-vnd.vos-LabelAs" add-ons/tracker/label_as)
