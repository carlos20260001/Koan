/*
 * Copyright 2026, Koan contributors.
 * Distributed under the terms of the MIT License.
 */

// Loads the translator add-ons from one directory through BTranslatorRoster
// and decodes the PNG, JPEG and GIF test images into the native bitmap
// format, checking the identified type and the decoded header.
//
// Usage: TranslatorDecodeTest <translators directory> <data directory>
// Exit status: 0 passed, 1 failed, 77 skipped (no Nexus devices).


#include <ByteOrder.h>
#include <DataIO.h>
#include <File.h>
#include <String.h>
#include <TranslatorFormats.h>
#include <TranslatorRoster.h>

#include <stdio.h>
#include <string.h>


struct TestImage {
	const char*	name;
	const char*	mime;
	int32		width;
	int32		height;
};


static const TestImage kImages[] = {
	{ "image.png", "image/png", 384, 289 },
	{ "image.jpg", "image/jpeg", 384, 289 },
	{ "image.gif", "image/gif", 384, 289 },
};


static bool
decode_image(BTranslatorRoster& roster, const char* dataDir,
	const TestImage& image)
{
	BString path(dataDir);
	path << "/" << image.name;

	BFile file(path.String(), B_READ_ONLY);
	if (file.InitCheck() != B_OK) {
		printf("FAIL %s: cannot open: %s\n", image.name,
			strerror(file.InitCheck()));
		return false;
	}

	translator_info info;
	status_t status = roster.Identify(&file, NULL, &info, 0, NULL,
		B_TRANSLATOR_BITMAP);
	if (status != B_OK) {
		printf("FAIL %s: Identify: %s\n", image.name, strerror(status));
		return false;
	}
	if (strcmp(info.MIME, image.mime) != 0) {
		printf("FAIL %s: identified as \"%s\" by \"%s\", expected \"%s\"\n",
			image.name, info.MIME, info.name, image.mime);
		return false;
	}

	file.Seek(0, SEEK_SET);
	BMallocIO output;
	status = roster.Translate(&file, &info, NULL, &output,
		B_TRANSLATOR_BITMAP);
	if (status != B_OK) {
		printf("FAIL %s: Translate: %s\n", image.name, strerror(status));
		return false;
	}

	TranslatorBitmap header;
	if (output.BufferLength() < sizeof(header)) {
		printf("FAIL %s: %zu bytes of output\n", image.name,
			output.BufferLength());
		return false;
	}
	memcpy(&header, output.Buffer(), sizeof(header));
	if (swap_data(B_UINT32_TYPE, &header, sizeof(header),
			B_SWAP_BENDIAN_TO_HOST) != B_OK) {
		printf("FAIL %s: cannot swap the bitmap header\n", image.name);
		return false;
	}

	int32 width = header.bounds.IntegerWidth() + 1;
	int32 height = header.bounds.IntegerHeight() + 1;
	size_t expected = sizeof(header) + (size_t)header.dataSize;
	if (header.magic != B_TRANSLATOR_BITMAP || width != image.width
		|| height != image.height
		|| header.rowBytes < (uint32)width
		|| header.dataSize < header.rowBytes * (uint32)height
		|| output.BufferLength() != expected) {
		printf("FAIL %s: magic 0x%08" B_PRIx32 ", %" B_PRId32 "x%" B_PRId32
			", rowBytes %" B_PRIu32 ", dataSize %" B_PRIu32 ", %zu bytes\n",
			image.name, header.magic, width, height, header.rowBytes,
			header.dataSize, output.BufferLength());
		return false;
	}

	printf("PASS %s: \"%s\" -> %" B_PRId32 "x%" B_PRId32 " bitmap,"
		" color space 0x%x\n", image.name, info.name, width, height,
		(unsigned)header.colors);
	return true;
}


int
main(int argc, char** argv)
{
	if (argc != 3) {
		fprintf(stderr, "usage: %s <translators directory> <data directory>\n",
			argv[0]);
		return 1;
	}

	BTranslatorRoster roster;
	status_t status = roster.AddTranslators(argv[1]);
	if (status != B_OK) {
		printf("FAIL: no translator loaded from %s: %s\n", argv[1],
			strerror(status));
		return 1;
	}

	int failed = 0;
	for (size_t i = 0; i < sizeof(kImages) / sizeof(kImages[0]); i++) {
		if (!decode_image(roster, argv[2], kImages[i]))
			failed++;
	}

	return failed == 0 ? 0 : 1;
}
