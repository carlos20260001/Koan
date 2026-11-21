# Copyright 2026, Koan contributors. All rights reserved.
# Distributed under the terms of the MIT License.

Application(
	FileTypes

	SOURCES
	FileTypes.cpp
	FileTypesWindow.cpp
	ApplicationTypesWindow.cpp
	AttributeListView.cpp
	DropTargetListView.cpp
	MimeTypeListView.cpp
	NewFileTypeWindow.cpp
	AttributeWindow.cpp
	ExtensionWindow.cpp
	ApplicationTypeWindow.cpp
	FileTypeWindow.cpp
	TypeListWindow.cpp
	IconView.cpp
	PreferredAppMenu.cpp
	StringView.cpp

	LIBS tracker localestub
	RDEF FileTypes.rdef FileTypes.icons.rdef
)

UsePrivateHeaders(FileTypes app interface shared tracker)
target_include_directories(FileTypes PRIVATE
	${PROJECT_SOURCE_DIR}/headers/libs/icon
	${PROJECT_SOURCE_DIR}/src/kits/tracker)
DoCatalogs("x-vnd.vos-FileTypes" preferences/filetypes)
