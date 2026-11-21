# Copyright 2026, Koan contributors. All rights reserved.
# Distributed under the terms of the MIT License.

# libnotification.a is the notification server's static library
# (src/servers/notification).
if(NOT TARGET notification)
	return()
endif()

Application(
	Notifications

	SOURCES
	AppRefFilter.cpp
	Notifications.cpp
	PrefletWin.cpp
	PrefletView.cpp
	SettingsPane.cpp
	GeneralView.cpp
	NotificationsView.cpp

	LIBS translation tracker notification localestub
	RDEF Notifications.rdef
)

UsePrivateHeaders(Notifications interface notification)
target_include_directories(Notifications PRIVATE
	${PROJECT_SOURCE_DIR}/src/servers/notification)
DoCatalogs("x-vnd.vos-Notifications" preferences/notifications)
