# Copyright 2026, Koan contributors. All rights reserved.
# Distributed under the terms of the MIT License.

# The greeter: janus_session starts it as "vitruvian-login" once
# /var/lib/vos/first-boot-done exists, and janus runs it with --unlock to
# unlock the screen. Passwords are checked by janus through PAM.
if(NOT TARGET localestub)
	message(STATUS "Login: skipped, needs localestub")
	return()
endif()

Application(
	Login

	SOURCES
	DesktopWindow.cpp
	LoginApp.cpp
	LoginWindow.cpp
	LoginView.cpp
	main.cpp

	LIBS localestub
	RDEF Login.rdef
)

UsePrivateHeaders(Login app interface kernel)
DoCatalogs("x-vnd.vos-Login" apps/login)
