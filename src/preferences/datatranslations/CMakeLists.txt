# Copyright 2026, Koan contributors. All rights reserved.
# Distributed under the terms of the MIT License.

Application(
	DataTranslations

	SOURCES
	DataTranslations.cpp
	DataTranslationsWindow.cpp
	DataTranslationsSettings.cpp
	TranslatorListView.cpp

	LIBS translation shared localestub
	RDEF DataTranslations.rdef
)

UsePrivateHeaders(DataTranslations shared)
DoCatalogs("x-vnd.vos-DataTranslations" preferences/datatranslations)
