# Copyright 2026, Koan contributors. All rights reserved.
# Distributed under the terms of the MIT License.

# air/OS's Mail on its mail kit (libmail, in /lib), which talks to
# mail_daemon.
foreach(_lib mail tracker shared textencoding localestub)
	if(NOT TARGET ${_lib})
		message(STATUS "Mail: skipped, needs ${_lib}")
		return()
	endif()
endforeach()

Application(
	Mail

	SOURCES
	AddressTextControl.cpp
	Content.cpp
	Enclosures.cpp
	FindWindow.cpp
	Header.cpp
	LabelWindow.cpp
	MailApp.cpp
	MailPopUpMenu.cpp
	MailSupport.cpp
	MailWindow.cpp
	MessageStatus.cpp
	People.cpp
	Prefs.cpp
	QueryList.cpp
	QueryMenu.cpp
	Signature.cpp
	Utilities.cpp
	WIndex.cpp
	Words.cpp
	KUndoBuffer.cpp

	# autocompletion, shared with WebPositive
	${PROJECT_SOURCE_DIR}/src/apps/webpositive/autocompletion/AutoCompleter.cpp
	${PROJECT_SOURCE_DIR}/src/apps/webpositive/autocompletion/AutoCompleterDefaultImpl.cpp
	${PROJECT_SOURCE_DIR}/src/apps/webpositive/autocompletion/TextViewCompleter.cpp

	LIBS shared tracker mail textencoding localestub
	RDEF Mail.rdef pictures.rdef
)

UsePrivateHeaders(Mail interface shared storage textencoding)
target_include_directories(Mail PRIVATE
	${PROJECT_SOURCE_DIR}/headers/private/mail
	${PROJECT_SOURCE_DIR}/src/apps/webpositive/autocompletion
)

DoCatalogs("x-vnd.Be-MAIL" apps/mail)
