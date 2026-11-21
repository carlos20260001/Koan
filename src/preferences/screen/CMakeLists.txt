# Copyright 2026, Koan contributors. All rights reserved.
# Distributed under the terms of the MIT License.

# air/OS's multi-monitor Screen preferences on the merged interface kit:
# BScreen, the display_output API, render scale and the display layout,
# which app_server's KMS/DRM back ends implement.
Application(
	Screen

	SOURCES
	AlertWindow.cpp
	DisplayArrangementView.cpp
	DisplayLayoutState.cpp
	IdentifyWindow.cpp
	multimon.cpp
	RefreshSlider.cpp
	RefreshWindow.cpp
	ScreenApplication.cpp
	ScreenMode.cpp
	ScreenSettings.cpp
	ScreenWindow.cpp
	Utility.cpp

	# CVT timings for custom modes (Haiku linked libaccelerantscommon.a,
	# which this tree builds only with VOS_WITH_HAIKU_GPU_DRIVERS)
	../../add-ons/accelerants/common/compute_display_timing.cpp

	LIBS localestub
	RDEF Screen.rdef
)

UsePrivateHeaders(Screen graphics/common interface preferences)
# Radeon's multi-monitor settings tunnel (multimon.cpp): app_server's
# Linux back ends answer the tunnelled ProposeMode() with an error, so the
# panel hides those menus.
target_include_directories(Screen PRIVATE
	${PROJECT_SOURCE_DIR}/headers/private/graphics/radeon)
DoCatalogs("x-vnd.vos-Screen" preferences/screen)
