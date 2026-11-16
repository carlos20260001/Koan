/*
 * Copyright 2026, Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 */


#include <errno.h>
#include <stdio.h>
#include <sys/auxv.h>

#include <OS.h>


int
main()
{
	int failures = 0;

	errno = 0;
	unsigned long pageSize = getauxval(AT_PAGESZ);
	printf("AT_PAGESZ %lu\n", pageSize);
	if (pageSize != B_PAGE_SIZE || errno != 0)
		failures++;

	errno = 0;
	if (getauxval(12345) != 0 || errno != ENOENT) {
		printf("an unknown type should give 0 and ENOENT\n");
		failures++;
	}

#ifdef __aarch64__
	unsigned long hwcap = getauxval(AT_HWCAP);
	printf("AT_HWCAP %#lx AT_HWCAP2 %#lx, LSE atomics %s\n", hwcap,
		getauxval(AT_HWCAP2), (hwcap & HWCAP_ATOMICS) != 0 ? "yes" : "no");
	if ((hwcap & (HWCAP_FP | HWCAP_ASIMD)) != (HWCAP_FP | HWCAP_ASIMD)) {
		printf("every arm64 CPU we run on has FP and AdvSIMD\n");
		failures++;
	}
#endif

	printf("%s\n", failures == 0 ? "passed" : "FAILED");
	return failures != 0;
}
