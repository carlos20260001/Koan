/*
 * Copyright 2026, Koan contributors. All rights reserved.
 * Distributed under the terms of the MIT License.
 */


#include "LinuxSwap.h"

#include <stdio.h>
#include <string.h>
#include <sys/stat.h>


namespace LinuxSwap {


const char*
DefaultSwapVolumePath()
{
	return "/";
}


const char*
DefaultSwapFilePath()
{
	return "/var/swap";
}


status_t
GetActiveSwap(bool& enabled, off_t& totalSize, dev_t& fileVolume)
{
	enabled = false;
	totalSize = 0;
	fileVolume = -1;

	FILE* swaps = fopen("/proc/swaps", "r");
	if (swaps == NULL)
		return B_ENTRY_NOT_FOUND;

	// Filename Type Size(KiB) Used(KiB) Priority
	char line[1024];
	bool header = true;
	while (fgets(line, sizeof(line), swaps) != NULL) {
		if (header) {
			header = false;
			continue;
		}
		char name[512];
		char type[32];
		long long sizeKiB = 0;
		if (sscanf(line, "%511s %31s %lld", name, type, &sizeKiB) != 3)
			continue;

		enabled = true;
		totalSize += (off_t)sizeKiB * 1024;
		if (fileVolume < 0 && strcmp(type, "file") == 0) {
			// /proc/swaps escapes blanks as \040; such a name simply
			// fails to stat.
			struct stat st;
			if (stat(name, &st) == 0)
				fileVolume = st.st_dev;
		}
	}
	fclose(swaps);
	return B_OK;
}


status_t
Apply(bool enabled, off_t size, dev_t volume)
{
	// TODO(linux backend): creating, resizing and switching swap files
	// needs root: a polkit-guarded helper that runs fallocate/mkswap/
	// swapon (or swapoff and removes the file) for <volume>/swap or
	// /var/swap, and keeps one fstab line or systemd swap unit for it.
	// Until it exists the settings are only saved.
	(void)enabled;
	(void)size;
	(void)volume;
	return B_NOT_SUPPORTED;
}


}	// namespace LinuxSwap
