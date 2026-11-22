# Copyright 2026, Koan contributors.
# Distributed under the terms of the MIT License.

# The Jamfile names a Lpstyl.rdef that was never committed; built without
# resources.
PrintDriverAddOn(print_lpstyl "Lpstyl"
	SOURCES
	Lpstyl.cpp
	LpstylCap.cpp
	LpstylData.cpp
	LpstylEntry.cpp
)
