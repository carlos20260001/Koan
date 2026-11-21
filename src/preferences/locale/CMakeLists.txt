# Copyright 2026, Koan contributors. All rights reserved.
# Distributed under the terms of the MIT License.

Application(
	Locale

	SOURCES
	LanguageListView.cpp
	LocalePreflet.cpp
	LocaleWindow.cpp
	FormatSettingsView.cpp

	LIBS shared localestub
	RDEF Locale.rdef
)

UsePrivateHeaders(Locale interface locale)
DoCatalogs("x-vnd.vos-Locale" preferences/locale)
