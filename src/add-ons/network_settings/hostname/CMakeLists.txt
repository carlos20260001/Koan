# Copyright 2026, Koan contributors.
# Distributed under the terms of the MIT License.

# The host name, set through systemd-hostnamed (D-Bus, GIO).
find_package(PkgConfig REQUIRED)
pkg_check_modules(HOSTNAME_GIO REQUIRED gio-2.0)

NetworkSettingsAddOn(Hostname
	SOURCES
	HostnameAddOn.cpp
	HostnameView.cpp

	RDEF Hostname.rdef
)
# The chroot's /usr/include stays behind the C++ headers (-isystem).
list(FILTER HOSTNAME_GIO_INCLUDE_DIRS EXCLUDE REGEX "/usr/include/?$")
target_include_directories(Hostname PRIVATE ${HOSTNAME_GIO_INCLUDE_DIRS})
target_link_libraries(Hostname PRIVATE ${HOSTNAME_GIO_LINK_LIBRARIES})
DoCatalogs("x-vnd.vos-Hostname" add-ons/network_settings/hostname)
