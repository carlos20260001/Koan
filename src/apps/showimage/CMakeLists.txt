# Copyright 2026, Koan contributors. All rights reserved.
# Distributed under the terms of the MIT License.

foreach(_lib shared game tracker translation localestub)
	if(NOT TARGET ${_lib})
		message(STATUS "ShowImage: skipped, needs ${_lib}")
		return()
	endif()
endforeach()

Application(
	ShowImage

	SOURCES
	Filter.cpp
	ImageCache.cpp
	ImageFileNavigator.cpp
	PrintOptionsWindow.cpp
	InfoStatusView.cpp
	ProgressStatusView.cpp
	SelectionBox.cpp
	ShowImageApp.cpp
	ShowImageSettings.cpp
	ShowImageStatusView.cpp
	ShowImageView.cpp
	ShowImageWindow.cpp
	SupportingAppsMenu.cpp
	ToolBarIcons.cpp

	LIBS shared game tracker translation localestub
	INCLUDES "${PROJECT_SOURCE_DIR}/src/kits/tracker"
		"${PROJECT_SOURCE_DIR}/headers/os/add-ons/tracker"
		"${PROJECT_SOURCE_DIR}/headers/libs/icon"
	RDEF ShowImage.rdef
)

UsePrivateHeaders(ShowImage app interface tracker shared system kernel)
DoCatalogs("x-vnd.vos-ShowImage" apps/showimage)
