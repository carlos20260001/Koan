# Copyright 2026, Koan contributors. All rights reserved.
# Distributed under the terms of the MIT License.

# A Deskbar applet: Deskbar loads this file with load_add_on() and calls
# instantiate_deskbar_item(), and it also runs as the application that
# installs the item, so it is a RunnableAddOn (dlopen-able and execve-able).
if(NOT TARGET localestub)
	message(STATUS "AutoRaise: skipped, needs localestub")
	return()
endif()

RunnableAddOn(
	AutoRaise

	SOURCES
	AutoRaiseApp.cpp
	AutoRaiseIcon.cpp
	settings.cpp

	LIBS localestub
	RDEF AutoRaise.rdef
)

UsePrivateHeaders(AutoRaise interface shared)
DoCatalogs("x-vnd.mmu.AutoRaise" apps/autoraise)
