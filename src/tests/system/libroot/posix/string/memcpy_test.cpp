/*
 * Copyright 2026, Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

/*!	memcpy_test: checks memcpy() for sizes from 0 to 70000 bytes at every
	distance between source and destination modulo 64, and near a multiple
	of the page size, where x86_64 uses another copy loop. Then prints the
	speed of large copies by that distance.
*/


#include <OS.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>


static void* (*volatile sMemcpy)(void*, const void*, size_t) = memcpy;


static int
check(uint8_t* buffer, size_t sourceOffset, size_t destinationOffset,
	size_t length)
{
	uint8_t* source = buffer + sourceOffset;
	uint8_t* destination = buffer + destinationOffset;
	for (size_t i = 0; i < length; i++)
		source[i] = (uint8_t)(i * 7 + length + sourceOffset);
	memset(destination - 16, 0xee, length + 32);

	sMemcpy(destination, source, length);

	for (size_t i = 0; i < 16; i++) {
		if (destination[-1 - (ssize_t)i] != 0xee
			|| destination[length + i] != 0xee) {
			printf("FAIL: wrote outside, length %zu, distance %zd\n", length,
				(ssize_t)(destinationOffset - sourceOffset));
			return 1;
		}
	}
	for (size_t i = 0; i < length; i++) {
		if (destination[i] != (uint8_t)(i * 7 + length + sourceOffset)) {
			printf("FAIL: byte %zu of %zu, distance %zd\n", i, length,
				(ssize_t)(destinationOffset - sourceOffset));
			return 1;
		}
	}
	return 0;
}


int
main()
{
	const size_t kBufferSize = 1 << 20;
	uint8_t* buffer = (uint8_t*)aligned_alloc(B_PAGE_SIZE, kBufferSize);
	if (buffer == NULL)
		return 1;

	const size_t lengths[] = { 0, 1, 7, 8, 15, 16, 17, 63, 64, 100, 2047,
		2048, 2049, 4095, 4096, 4100, 16383, 16384, 65536, 70000 };
	const size_t kDestination = 512 * 1024;
	int failures = 0;
	for (size_t length : lengths) {
		for (size_t distance = 0; distance < 64; distance++) {
			failures += check(buffer, 64, kDestination + distance, length);
			// just below a page multiple
			failures += check(buffer, 64 + distance, kDestination + 64,
				length);
		}
	}

	const size_t kLength = 16384;
	const size_t distances[] = { 0, 4, 8, 15, 16, 64, 4080, 4092 };
	printf("memcpy of %zu bytes, MB/s by distance modulo the page size:\n",
		kLength);
	for (size_t distance : distances) {
		uint8_t* source = buffer;
		uint8_t* destination = buffer + kDestination
			+ (distance == 4092 || distance == 4080 ? distance - 4096 : distance);
		bigtime_t start = system_time();
		for (int i = 0; i < 4096; i++)
			sMemcpy(destination, source, kLength);
		bigtime_t elapsed = system_time() - start;
		printf("  %4zu: %6.0f\n", distance,
			4096.0 * kLength / (elapsed > 0 ? elapsed : 1));
	}

	printf("%s\n", failures == 0 ? "PASS" : "FAIL");
	free(buffer);
	return failures == 0 ? 0 : 1;
}
