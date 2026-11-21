# Copyright 2026, Koan contributors. All rights reserved.
# Distributed under the terms of the MIT License.

Application(
	VirtualMemory

	SOURCES
	VirtualMemory.cpp
	SettingsWindow.cpp
	Settings.cpp
	LinuxSwap.cpp

	LIBS shared localestub
	RDEF VirtualMemory.rdef
)

UsePrivateHeaders(VirtualMemory shared system)
DoCatalogs("x-vnd.vos-VirtualMemory" preferences/virtualmemory)
