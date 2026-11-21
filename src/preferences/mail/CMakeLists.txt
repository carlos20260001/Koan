# Copyright 2026, Koan contributors. All rights reserved.
# Distributed under the terms of the MIT License.

# E-mail: account and filter settings of air/OS's mail daemon (libmail).
if(NOT TARGET mail)
	return()
endif()

Application(
	E-mail

	SOURCES
	AutoConfig.cpp
	AutoConfigView.cpp
	AutoConfigWindow.cpp
	ConfigViews.cpp
	ConfigWindow.cpp
	FilterList.cpp
	DNSQuery.cpp
	FilterConfigView.cpp
	main.cpp

	LIBS mail localestub
	RDEF e-mail.rdef
)

UsePrivateHeaders(E-mail shared)
target_include_directories(E-mail PRIVATE
	${PROJECT_SOURCE_DIR}/headers/private/mail
	${PROJECT_SOURCE_DIR}/headers/os/mail
	${PROJECT_SOURCE_DIR}/headers/os/add-ons/mail_daemon)
DoCatalogs("x-vnd.vos-Mail" preferences/mail)

add_subdirectory(ProviderInfo)
