# Copyright 2026, Koan contributors. All rights reserved.
# Distributed under the terms of the MIT License.

# An application and an add-on in one file: process_refs() comes from
# TrackerAddOnAppLaunch.h and launches this same file with the refs.
RunnableAddOn(
	ZipOMatic

	SOURCES
	GenericThread.cpp
	ZipOMatic.cpp
	ZipOMaticActivity.cpp
	ZipOMaticWindow.cpp
	ZipperThread.cpp

	LIBS tracker localestub

	RDEF ZipOMatic.rdef
)

UsePrivateHeaders(ZipOMatic tracker shared)
DoCatalogs("x-vnd.vos.zip-o-matic" add-ons/tracker/zipomatic)
