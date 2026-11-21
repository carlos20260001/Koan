# Copyright 2026, Koan contributors. All rights reserved.
# Distributed under the terms of the MIT License.

# air/OS's SoftwareUpdater on its hpkg package kit (libpackage, target
# "bpackage"); the network kit (libnetapi, libnetservices) is in libbe.
# TODO(linux backend): BPackageManager needs the libsolv solver add-on
# (not built: Debian's libsolv has no repo_haiku) and package_daemon (built,
# not started; Debian packages are apt's), so a check ends in the kit's
# error message on Linux.
foreach(_lib bpackage shared translation localestub)
	if(NOT TARGET ${_lib})
		message(STATUS "SoftwareUpdater: skipped, needs ${_lib}")
		return()
	endif()
endforeach()

Application(
	SoftwareUpdater

	SOURCES
	SoftwareUpdaterApp.cpp
	SoftwareUpdaterWindow.cpp
	UpdateAction.cpp
	UpdateManager.cpp
	CheckAction.cpp
	CheckManager.cpp
	WorkingLooper.cpp

	# package_daemon
	${PROJECT_SOURCE_DIR}/src/servers/package/ProblemWindow.cpp

	LIBS bpackage shared translation localestub
	RDEF SoftwareUpdater.rdef
)

UsePrivateHeaders(SoftwareUpdater app interface shared)
target_include_directories(SoftwareUpdater PRIVATE
	${PROJECT_SOURCE_DIR}/headers/private/package
	${PROJECT_SOURCE_DIR}/src/servers/package
)

DoCatalogs("x-vnd.vos-softwareupdater" apps/softwareupdater)
