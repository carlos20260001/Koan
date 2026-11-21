# Copyright 2026, Koan contributors. All rights reserved.
# Distributed under the terms of the MIT License.

foreach(_lib tracker shared translation textencoding localestub)
	if(NOT TARGET ${_lib})
		message(STATUS "StyledEdit: skipped, needs ${_lib}")
		return()
	endif()
endforeach()

Application(
	StyledEdit

	SOURCES
	ColorMenuItem.cpp
	FindWindow.cpp
	ReplaceWindow.cpp
	StatusView.cpp
	StyledEditApp.cpp
	StyledEditView.cpp
	StyledEditWindow.cpp

	LIBS shared translation tracker textencoding localestub
	RDEF StyledEdit.rdef
)

UsePrivateHeaders(StyledEdit app interface shared system textencoding tracker)
target_include_directories(StyledEdit PRIVATE
	${PROJECT_SOURCE_DIR}/headers/os/add-ons/tracker
	${PROJECT_SOURCE_DIR}/src/kits/tracker
)

DoCatalogs("x-vnd.vos-StyledEdit" apps/stylededit)
