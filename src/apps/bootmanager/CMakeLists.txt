# Copyright 2026, Koan contributors. All rights reserved.
# Distributed under the terms of the MIT License.
#
# BootManager: the wizard that puts Haiku's boot menu (bootman.S) into a
# disk's master boot record. It finds the disks and partitions through the
# storage kit's disk device API, which libroot2 serves from udev/blkid.
#
# This system boots through GRUB (or the EFI firmware loading GRUB), and its
# boot entries come from data/etc/grub.d/15_vos. Writing Haiku's boot menu
# over the MBR would replace GRUB's boot code, so the actions that write a
# disk (install the menu, restore a saved MBR) are stubbed in
# LegacyBootMenu.cpp, TODO(linux backend): a BootMenu that writes the chosen
# entries, default and timeout as a grub.d fragment and /etc/default/grub
# settings through a polkit root helper, then runs update-grub. Saving the
# MBR to a file only reads the disk and works as before.
#
# bootman.S is no longer assembled: no part of the Linux build writes it to
# a disk, and NASM is not in the build image.

foreach(_lib tracker textencoding shared localestub)
	if(NOT TARGET ${_lib})
		message(STATUS "BootManager: skipped, needs ${_lib}")
		return()
	endif()
endforeach()

Application(
	BootManager

	SOURCES
	BootDrive.cpp
	BootManager.cpp
	BootManagerController.cpp
	BootManagerWindow.cpp
	DefaultPartitionPage.cpp
	DrivesPage.cpp
	FileSelectionPage.cpp
	LegacyBootMenu.cpp
	PartitionsPage.cpp
	UninstallPage.cpp
	WizardView.cpp
	DescriptionPage.cpp
	WizardController.cpp
	WizardPageView.cpp

	LIBS textencoding tracker shared localestub
	RDEF BootManager.rdef
)

UsePrivateHeaders(BootManager shared storage tracker interface)
DoCatalogs("x-vnd.vos-BootManager" apps/bootmanager)
