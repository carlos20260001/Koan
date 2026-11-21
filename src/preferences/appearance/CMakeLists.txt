# Copyright 2026, Koan contributors. All rights reserved.
# Distributed under the terms of the MIT License.

Application(
	Appearance

	SOURCES
	AntialiasingSettingsView.cpp
	Appearance.cpp
	AppearanceWindow.cpp
	Colors.cpp
	ColorsView.cpp
	FakeScrollBar.cpp
	FontSelectionView.cpp
	FontView.cpp
	LookAndFeelSettingsView.cpp

	LIBS shared localestub
	RDEF Appearance.rdef
)

UsePrivateHeaders(Appearance shared app interface)
target_include_directories(Appearance PRIVATE
	${PROJECT_SOURCE_DIR}/headers/private/servers/app
	${PROJECT_SOURCE_DIR}/src/servers/app)
DoCatalogs("x-vnd.vos-Appearance" preferences/appearance)
