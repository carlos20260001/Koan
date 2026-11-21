# Copyright 2026, Koan contributors. All rights reserved.
# Distributed under the terms of the MIT License.
#
# LegacyPackageInstaller: installs BeOS/Zeta .pkg archives (the
# "application/x-scode-UPkg" type). Despite the directory name it is not
# the hpkg installer and does not use the package kit. The archives' zlib
# streams are inflated with the system zlib; the /boot/... destinations a
# package names are mapped through find_directory(), which libroot2
# resolves to this system's directories.

foreach(_lib tracker translation shared localestub)
	if(NOT TARGET ${_lib})
		message(STATUS "LegacyPackageInstaller: skipped, needs ${_lib}")
		return()
	endif()
endforeach()
find_package(ZLIB QUIET)
if(NOT ZLIB_FOUND)
	message(STATUS "LegacyPackageInstaller: skipped, needs zlib")
	return()
endif()

Application(
	LegacyPackageInstaller

	SOURCES
	main.cpp
	BlockingWindow.cpp
	PackageWindow.cpp
	PackageView.cpp
	PackageInfo.cpp
	PackageItem.cpp
	PackageInstall.cpp
	PackageStatus.cpp
	PackageTextViewer.cpp
	PackageImageViewer.cpp
	InstalledPackageInfo.cpp
	UninstallWindow.cpp
	UninstallView.cpp

	LIBS localestub tracker translation shared ZLIB::ZLIB
	RDEF PackageInstaller.rdef
)

UsePrivateHeaders(LegacyPackageInstaller shared interface)
DoCatalogs("x-vnd.vos-PackageInstaller" apps/packageinstaller)
