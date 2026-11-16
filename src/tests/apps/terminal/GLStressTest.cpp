/*
 * Copyright 2026, Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 */


/*!	Drives Terminal's OpenGL renderer add-on with made up frames, to find
	out whether (and how fast) a GPU and its driver can draw them.

	usage: GLStressTest <add-on> [width height frames]
*/


#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <Application.h>
#include <OS.h>
#include <image.h>

#include <memory>

#include "TermRenderer.h"


int
main(int argc, char** argv)
{
	if (argc < 2) {
		fprintf(stderr, "usage: %s <add-on> [width height frames]\n",
			argv[0]);
		return 1;
	}
	int32 width = argc > 2 ? atoi(argv[2]) : 3792;
	int32 height = argc > 3 ? atoi(argv[3]) : 2056;
	int32 frames = argc > 4 ? atoi(argv[4]) : 2000;

	BApplication app("application/x-vnd.vos-TerminalGLStressTest");

	image_id addOn = load_add_on(argv[1]);
	create_terminal_renderer_func create;
	if (addOn < 0 || get_image_symbol(addOn, TERMINAL_RENDERER_ENTRY,
			B_SYMBOL_TYPE_TEXT, (void**)&create) != B_OK) {
		fprintf(stderr, "cannot load %s\n", argv[1]);
		return 1;
	}
	const char* error = NULL;
	TermRenderer* renderer = create(&error);
	if (renderer == NULL) {
		fprintf(stderr, "no renderer: %s\n", error);
		return 1;
	}
	printf("%s, %dx%d, %d frames\n", renderer->Name(), (int)width,
		(int)height, (int)frames);

	// a glyph atlas of noise
	static const int32 kPageSize = 2048;
	uint8* page = (uint8*)malloc(kPageSize * kPageSize);
	for (int32 i = 0; i < kPageSize * kPageSize; i++)
		page[i] = (i * 2654435761u) >> 24;
	const uint8* pages[1] = { page };
	GlyphAtlasInfo::Dirty dirty[1] = { { 0, kPageSize } };
	GlyphAtlasInfo atlas;
	memset(&atlas, 0, sizeof(atlas));
	atlas.pageSize = kPageSize;
	atlas.cellWidth = 14;
	atlas.cellHeight = 34;
	atlas.slotsPerRow = kPageSize / atlas.cellWidth;
	atlas.slotsPerPage = atlas.slotsPerRow * (kPageSize / atlas.cellHeight);
	atlas.pageCount = 1;
	atlas.pages = pages;
	atlas.dirty = dirty;

	RenderFrame frame;
	frame.metrics.cellWidth = 14;
	frame.metrics.cellHeight = 34;
	frame.metrics.baseline = 26;
	frame.metrics.underlinePosition = 28;
	frame.metrics.underlineThickness = 2;
	frame.metrics.strikePosition = 18;
	frame.metrics.lineThickness = 2;
	frame.width = width;
	frame.height = height;
	frame.columns = width / 14;
	frame.rows = height / 34;
	frame.cells.resize((size_t)frame.columns * frame.rows);
	frame.dirtyRows.assign(frame.rows, false);
	frame.defaultBackground = 0xffffff;
	frame.cursorShape = RENDER_CURSOR_BEAM;
	frame.cursorX = 3;
	frame.cursorY = 3;
	frame.cursorWidth = 1;
	frame.cursorColor = 0x000000;
	frame.imagesChanged = false;

	uint8* bits = (uint8*)malloc((size_t)width * height * 4);
	uint32 seed = 1;
	bigtime_t total = 0;
	bigtime_t longest = 0;
	bool resize = getenv("STRESS_RESIZE") != NULL;
	for (int32 f = 0; f < frames; f++) {
		if (resize && f % 50 == 25) {
			// a window being resized: another size every few frames
			int32 newWidth = f % 100 == 25 ? width / 2 : width;
			int32 newHeight = f % 100 == 25 ? height / 2 : height;
			frame.width = newWidth;
			frame.height = newHeight;
			frame.columns = newWidth / 14;
			frame.rows = newHeight / 34;
			frame.cells.assign((size_t)frame.columns * frame.rows,
				RenderCell());
			frame.dirtyRows.assign(frame.rows, false);
			frame.allDirty = true;
		}

		// every 16th frame everything changes (scrolling), else a few rows
		frame.allDirty = frame.allDirty || f % 16 == 0
			|| getenv("STRESS_FULL") != NULL;
		frame.dirtyRows.assign(frame.rows, false);
		int32 rows = frame.allDirty ? frame.rows : 1 + f % 4;
		for (int32 i = 0; i < rows; i++) {
			int32 row = frame.allDirty ? i : (seed = seed * 1103515245 + 12345)
				% frame.rows;
			frame.dirtyRows[row] = true;
			for (int32 x = 0; x < frame.columns; x++) {
				seed = seed * 1103515245 + 12345;
				RenderCell& cell = frame.Cell(row, x);
				cell.glyph = 1 + seed % 400;
				cell.foreground = seed & 0xffffff;
				cell.background = (seed >> 8) & 0xffffff;
				cell.decoration = 0xff0000;
				cell.flags = (seed >> 3) % 7 == 0 ? UNDERLINE_CURLY : 0;
				cell.foregroundAlpha = 255;
			}
		}

		if (getenv("STRESS_ATLAS") != NULL) {
			// new glyphs: a band of the atlas changes
			int32 top = (seed >> 4) % (kPageSize - 64);
			dirty[0].top = top;
			dirty[0].bottom = top + 34;
		}
		if (getenv("STRESS_IMAGE") != NULL && f % 30 == 0) {
			// an image, sometimes a new one
			static std::shared_ptr<ImageFrame> image;
			if (image == NULL || f % 90 == 0) {
				image.reset(new ImageFrame);
				image->pixels.assign(160 * 80 * 4, (uint8)(f & 0xff));
				image->gap = 0;
				image->serial = f + 1;
			}
			frame.images.clear();
			ImageDraw draw;
			draw.imageId = 1;
			draw.frame = image;
			draw.imageWidth = 160;
			draw.imageHeight = 80;
			draw.sourceX = draw.sourceY = 0;
			draw.sourceWidth = 160;
			draw.sourceHeight = 80;
			draw.x = 100;
			draw.y = 100 + (f % 10) * 34;
			draw.width = 320;
			draw.height = 160;
			draw.z = f % 60 == 0 ? -1 : 0;
			frame.images.push_back(draw);
			frame.allDirty = true;
		}

		bigtime_t start = system_time();
		int32 first, last;
		renderer->Render(frame, atlas, bits, frame.width * 4, first, last);
		frame.allDirty = false;
		bigtime_t elapsed = system_time() - start;
		total += elapsed;
		if (elapsed > longest)
			longest = elapsed;
		if (renderer->IsBroken()) {
			printf("FAILED at frame %d after %.1f ms\n", (int)f,
				elapsed / 1000.0);
			return 2;
		}
	}
	printf("OK: %d frames, %.2f ms on average, longest %.2f ms\n",
		(int)frames, total / 1000.0 / frames, longest / 1000.0);
	return 0;
}
