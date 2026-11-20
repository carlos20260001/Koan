/*
 * Copyright 2015, Axel Dörfler, axeld@pinc-software.de.
 * Distributed under the terms of the MIT License.
 */


//! Empty main temporary directory


#include "InitSharedMemoryDirectoryJob.h"


InitSharedMemoryDirectoryJob::InitSharedMemoryDirectoryJob()
	:
	AbstractEmptyDirectoryJob("init /var/shared_memory")
{
}


status_t
InitSharedMemoryDirectoryJob::Execute()
{
	// Linux: POSIX shared memory is /dev/shm, a tmpfs systemd mounts before
	// any of this runs. Haiku's ramfs at /var/shared_memory has no user here.
	return B_OK;
}
