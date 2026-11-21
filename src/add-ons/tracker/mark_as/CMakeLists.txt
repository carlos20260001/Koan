# Copyright 2026, Koan contributors. All rights reserved.
# Distributed under the terms of the MIT License.

# "Mark as…" (status submenu) and "Mark as Read". MarkAsNew.cpp is not
# built, as in the Jamfile this replaces.
TrackerAddOn(
	MarkAs SHARED

	SOURCES
	MarkAs.cpp

	LIBS be tracker mail

	RDEF MarkAs.rdef
)

set_target_properties(MarkAs PROPERTIES OUTPUT_NAME "Mark as…")
UsePrivateHeaders(MarkAs mail tracker)
# UsePrivateHeaders has no "mail" entry.
target_include_directories(MarkAs PRIVATE ${CMAKE_SOURCE_DIR}/headers/private/mail)

TrackerAddOn(
	MarkAsRead SHARED

	SOURCES
	MarkAsRead.cpp

	LIBS be tracker mail

	RDEF MarkAsRead.rdef
)

set_target_properties(MarkAsRead PROPERTIES OUTPUT_NAME "Mark as Read")
UsePrivateHeaders(MarkAsRead mail tracker)
# UsePrivateHeaders has no "mail" entry.
target_include_directories(MarkAsRead PRIVATE ${CMAKE_SOURCE_DIR}/headers/private/mail)
