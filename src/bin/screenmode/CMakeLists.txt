# Copyright 2026, Koan contributors. All rights reserved.
# Distributed under the terms of the MIT License.

# Shares the mode code of the Screen preferences. compute_display_timing
# stands in for libaccelerantscommon.a, built only with the optional GPU
# drivers.
set(_screen ${PROJECT_SOURCE_DIR}/src/preferences/screen)

Application(screenmode
	SOURCES
	screenmode.cpp
	${_screen}/multimon.cpp
	${_screen}/ScreenMode.cpp
	${PROJECT_SOURCE_DIR}/src/add-ons/accelerants/common/compute_display_timing.cpp

	INCLUDES
	${_screen}
	${PROJECT_SOURCE_DIR}/headers/private/graphics/radeon
	${PROJECT_SOURCE_DIR}/headers/private/osd
	RDEF screenmode.rdef
)
UsePrivateHeaders(screenmode graphics/common interface)
