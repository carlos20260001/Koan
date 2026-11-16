/*
 * Copyright 2003-2026, Haiku. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * Authors in chronological order:
 *		Sikosis
 *		Jérôme Duval
 */


#include "Media.h"

#include <stdio.h>

#include <File.h>
#include <FindDirectory.h>
#include <Path.h>
#include <Screen.h>

#include "MediaWindow.h"


static const char* kSettingsFile = "Media settings";
static const char* kOldSettingsFile = "MediaPrefs Settings";


static status_t
settings_path(BPath& path, const char* name)
{
	status_t status = find_directory(B_USER_SETTINGS_DIRECTORY, &path);
	if (status == B_OK)
		status = path.Append(name);
	return status;
}


status_t
load_media_settings(BMessage& settings)
{
	BPath path;
	status_t status = settings_path(path, kSettingsFile);
	if (status != B_OK)
		return status;

	BFile file(path.Path(), B_READ_ONLY);
	status = file.InitCheck();
	if (status == B_OK)
		status = settings.Unflatten(&file);
	return status;
}


status_t
save_media_settings(const BMessage& settings)
{
	BPath path;
	status_t status = settings_path(path, kSettingsFile);
	if (status != B_OK)
		return status;

	BFile file(path.Path(), B_WRITE_ONLY | B_CREATE_FILE | B_ERASE_FILE);
	status = file.InitCheck();
	if (status == B_OK)
		status = settings.Flatten(&file);
	return status;
}


//! Where the old Media preferences had their window.
static bool
old_window_position(BPoint& position)
{
	BPath path;
	if (settings_path(path, kOldSettingsFile) != B_OK)
		return false;

	FILE* file = fopen(path.Path(), "r");
	if (file == NULL)
		return false;

	bool found = false;
	char line[256];
	while (fgets(line, sizeof(line), file) != NULL) {
		int left, top, right, bottom;
		if (sscanf(line, " rect = %d,%d,%d,%d", &left, &top, &right, &bottom)
				== 4) {
			position.Set(left, top);
			found = true;
			break;
		}
	}
	fclose(file);
	return found;
}


Media::Media()
	:
	BApplication("application/x-vnd.vos-Media"),
	fWindow(NULL)
{
}


void
Media::ReadyToRun()
{
	float scale = std::max(1.0f, be_plain_font->Size() / 12.0f);
	BRect frame(0, 0, ceilf(760 * scale) - 1, ceilf(540 * scale) - 1);
	frame.OffsetTo(40, 40);

	BMessage settings;
	const char* page = NULL;
	BRect savedFrame;
	if (load_media_settings(settings) == B_OK) {
		if (settings.FindRect("frame", &savedFrame) == B_OK
			&& savedFrame.IsValid() && savedFrame.Width() >= 100
			&& savedFrame.Height() >= 100) {
			frame = savedFrame;
		}
		settings.FindString("page", &page);
	} else {
		BPoint position;
		if (old_window_position(position))
			frame.OffsetTo(position);
	}

	// do not open taller or wider than the screen it is on
	BRect screen = BScreen().Frame();
	frame.right = std::min(frame.right, frame.left + screen.Width() - 16);
	frame.bottom = std::min(frame.bottom, frame.top + screen.Height() - 40);

	fWindow = new MediaWindow(frame, page);
	fWindow->MoveOnScreen(B_MOVE_IF_PARTIALLY_OFFSCREEN);
	fWindow->Show();
}


//	#pragma mark -


int
main(int, char**)
{
	Media app;
	app.Run();
	return 0;
}
