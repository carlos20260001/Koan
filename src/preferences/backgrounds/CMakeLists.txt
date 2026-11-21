# Copyright 2026, Koan contributors. All rights reserved.
# Distributed under the terms of the MIT License.

Application(
	Backgrounds

	SOURCES
	BackgroundImage.cpp
	Backgrounds.cpp
	BackgroundsView.cpp
	ImageFilePanel.cpp

	LIBS tracker translation localestub
	RDEF Backgrounds.rdef
)

UsePrivateHeaders(Backgrounds preferences shared tracker)
DoCatalogs("x-vnd.vos-Backgrounds" preferences/backgrounds)
