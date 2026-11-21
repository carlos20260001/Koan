# Copyright 2026, Koan contributors. All rights reserved.
# Distributed under the terms of the MIT License.
#
# WebPositive: air/OS's browser. It is written against HaikuWebKit's
# BWebView API (WebPage.h, WebView.h, WebWindow.h, WebDownload.h,
# WebSettings.h, WebKitInfo.h, WebViewConstants.h; libWebKitLegacy.so.1).
# air/OS takes that API from the haikuwebkit HaikuPorts package or from the
# Summit browser's summit_webkitlegacy package (same classes on Summit's
# WebKit); neither is in this tree nor in Debian. This tree's own web kit
# (src/kits/web, headers/os/web) is VitruvianOS's WPE-based BWebView, a
# different API, so it cannot stand in.
#
# Point VOS_HAIKUWEBKIT_DIR at a HaikuWebKit build for Linux holding
# headers (<dir>/develop/headers or <dir>/include) and libWebKitLegacy
# (<dir>/lib, or <dir>/develop/lib); without it WebPositive is skipped.

set(VOS_HAIKUWEBKIT_DIR "" CACHE PATH
	"HaikuWebKit (or Summit webkitlegacy) for Linux, for WebPositive")

foreach(_lib tracker translation shared localestub)
	if(NOT TARGET ${_lib})
		message(STATUS "WebPositive: skipped, needs ${_lib}")
		return()
	endif()
endforeach()

find_path(VOS_HAIKUWEBKIT_INCLUDE_DIR WebPage.h
	HINTS "${VOS_HAIKUWEBKIT_DIR}"
	PATH_SUFFIXES develop/headers develop/headers/os include
	NO_DEFAULT_PATH)
find_library(VOS_HAIKUWEBKIT_LIBRARY
	NAMES WebKitLegacy WebKit
	HINTS "${VOS_HAIKUWEBKIT_DIR}"
	PATH_SUFFIXES lib develop/lib
	NO_DEFAULT_PATH)
if(NOT VOS_HAIKUWEBKIT_DIR OR NOT VOS_HAIKUWEBKIT_INCLUDE_DIR
		OR NOT VOS_HAIKUWEBKIT_LIBRARY)
	message(STATUS "WebPositive: skipped, needs HaikuWebKit's BWebView API "
		"(WebPage.h, libWebKitLegacy); set VOS_HAIKUWEBKIT_DIR")
	return()
endif()

Application(
	WebPositive

	SOURCES
	# autocompletion
	autocompletion/AutoCompleter.cpp
	autocompletion/AutoCompleterDefaultImpl.cpp
	autocompletion/TextViewCompleter.cpp

	# support
	support/BaseURL.cpp
	BookmarkBar.cpp
	support/FontSelectionView.cpp

	# tabview
	tabview/TabContainerView.cpp
	tabview/TabManager.cpp
	tabview/TabView.cpp

	AuthenticationPanel.cpp
	BrowserApp.cpp
	BrowserWindow.cpp
	BrowsingHistory.cpp
	ConsoleWindow.cpp
	CookieWindow.cpp
	CredentialsStorage.cpp
	DownloadProgressView.cpp
	DownloadWindow.cpp
	SettingsKeys.cpp
	SettingsWindow.cpp
	URLInputGroup.cpp

	LIBS ${VOS_HAIKUWEBKIT_LIBRARY} tracker translation localestub shared
	RDEF WebPositive.rdef
)

target_compile_options(WebPositive PRIVATE
	-Wno-error=sequence-point -Wno-error=format-truncation)
# BEFORE: headers/os/web (this tree's WPE web kit) has a WebView.h and a
# WebSettings.h of its own on the global path.
target_include_directories(WebPositive BEFORE PRIVATE
	${VOS_HAIKUWEBKIT_INCLUDE_DIR}
	${CMAKE_CURRENT_SOURCE_DIR}/autocompletion
	${CMAKE_CURRENT_SOURCE_DIR}/support
	${CMAKE_CURRENT_SOURCE_DIR}/tabview
)
target_include_directories(WebPositive PRIVATE
	${PROJECT_SOURCE_DIR}/headers/libs/icon
	${PROJECT_SOURCE_DIR}/headers/private/netservices
	${PROJECT_SOURCE_DIR}/src/kits/tracker
)
UsePrivateHeaders(WebPositive interface shared tracker)
# libWebKitLegacy's own dependencies, for the linker.
get_filename_component(_webkit_lib_dir "${VOS_HAIKUWEBKIT_LIBRARY}" DIRECTORY)
target_link_options(WebPositive PRIVATE "-Wl,-rpath-link,${_webkit_lib_dir}")
DoCatalogs("x-vnd.vos-WebPositive" apps/webpositive)
