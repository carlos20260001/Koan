/*
 * Copyright 2026, Koan contributors. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef LINUX_SWAP_H
#define LINUX_SWAP_H


#include <SupportDefs.h>


/*!	The Linux side of the VirtualMemory preferences. Haiku's kernel read
	the panel's settings at boot and made its swap file itself; on Linux the
	swap areas are those the kernel lists in /proc/swaps, set up by fstab or
	systemd swap units.
*/
namespace LinuxSwap {

// The volume a swap file is put on by default: the root file system.
const char*	DefaultSwapVolumePath();
// Where the swap file goes on the root volume.
const char*	DefaultSwapFilePath();

// What is active now, from /proc/swaps: whether any swap area is in use,
// their total size, and the device of the first swap file's volume (-1 for
// a swap partition or no swap).
status_t	GetActiveSwap(bool& enabled, off_t& totalSize, dev_t& fileVolume);

// Makes the active swap match the settings: swap file size and volume,
// on or off.
status_t	Apply(bool enabled, off_t size, dev_t volume);

}	// namespace LinuxSwap


#endif	// LINUX_SWAP_H
