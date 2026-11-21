/*
Open Tracker License

Terms and Conditions

Copyright (c) 1991-2000, Be Incorporated. All rights reserved.

Permission is hereby granted, free of charge, to any person obtaining a copy of
this software and associated documentation files (the "Software"), to deal in
the Software without restriction, including without limitation the rights to
use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies
of the Software, and to permit persons to whom the Software is furnished to do
so, subject to the following conditions:

The above copyright notice and this permission notice applies to all licensees
and shall be included in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF TITLE, MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
BE INCORPORATED BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN
AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF, OR IN CONNECTION
WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

Except as contained in this notice, the name of Be Incorporated shall not be
used in advertising or otherwise to promote the sale, use or other dealings in
this Software without prior written authorization from Be Incorporated.

Tracker(TM), Be(R), BeOS(R), and BeIA(TM) are trademarks or registered
trademarks of Be Incorporated in the United States and other countries. Other
brand product names are registered trademarks or trademarks of their respective
holders.
All rights reserved.
*/
/*
 * Copyright 2026, Koan contributors. All rights reserved.
 * Distributed under the terms of the MIT License.
 */


#include "DeskbarUtils.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <AppFileInfo.h>
#include <Debug.h>
#include <Directory.h>
#include <File.h>
#include <FilePanel.h>
#include <FindDirectory.h>
#include <List.h>
#include <Mime.h>
#include <NodeInfo.h>
#include <Screen.h>
#include <String.h>
#include <StringList.h>
#include <SymLink.h>
#include <Window.h>

#include "BarMenuBar.h"


const char* const kDeskbarMenuEntriesFileName = "menu_entries";

static const char* const kDeskbarDirectoryName = "deskbar";


void
AddRefsToDeskbarMenu(const BMessage* m, entry_ref* subdirectory)
{
	if (m) {
		int32 count = 0;
		uint32 type = 0;
		entry_ref ref;

		m->GetInfo("refs", &type, &count);
		if (count <= 0)
			return;

		BPath path;
		BSymLink link;
		BDirectory dir;
		if (subdirectory) {
			ref = *subdirectory;
			BEntry entry(&ref);
			if (entry.Exists()) {
				// if the ref is a file get the parent and convert it to a ref
				if (entry.IsFile()) {
					BEntry parent;
					entry.GetParent(&parent);
					parent.GetRef(&ref);
				}
			} else
				return;

			dir.SetTo(&ref);
		} else {
			if (find_directory(B_USER_DESKBAR_DIRECTORY, &path) == B_OK)
				dir.SetTo(path.Path());
			else
				return;
		}

		for (long i = 0; i < count; i++) {
			if (m->FindRef("refs", i, &ref) == B_NO_ERROR) {
				BEntry entry(&ref);
				entry.GetPath(&path);

				dir.CreateSymLink(ref.name, path.Path(), &link);
			}
		}
	}
}


status_t
GetDeskbarSettingsDirectory(BPath& _path, bool create)
{
	status_t error = find_directory(B_USER_SETTINGS_DIRECTORY, &_path, create);
	if (error != B_OK)
		return error;

	error = _path.Append(kDeskbarDirectoryName);
	if (error != B_OK)
		return error;

	return create ? create_directory(_path.Path(), 0755) : error;
}


status_t
GetDeskbarDataDirectory(BPath& _path)
{
	status_t error = find_directory(B_SYSTEM_DATA_DIRECTORY, &_path, false);
	if (error != B_OK)
		return error;

	return _path.Append(kDeskbarDirectoryName);
}


/*!	The system's menu_entries can only name fixed paths, but on Linux each
	user's home is a passwd entry (/home/<name>), not /boot/home. This writes
	a definition with this user's real menu directories to the settings
	directory, where TBarWindow::MenusBeginning() looks first.

	A file Deskbar wrote carries kGeneratedMarker and is refreshed when the
	directories change; one without the marker is the user's own and is
	left alone.
*/
status_t
UpdateUserDeskbarMenuEntries()
{
	static const char* const kGeneratedMarker
		= "# Written by Deskbar; remove this line to keep your changes.";
	static const directory_which kMenuDirectories[] = {
		B_USER_DESKBAR_DIRECTORY,
		B_USER_NONPACKAGED_DATA_DIRECTORY,
		B_USER_DATA_DIRECTORY,
		B_SYSTEM_NONPACKAGED_DATA_DIRECTORY,
		B_SYSTEM_DATA_DIRECTORY
	};

	BStringList paths;
	for (size_t i = 0; i < sizeof(kMenuDirectories)
			/ sizeof(kMenuDirectories[0]); i++) {
		BPath path;
		if (find_directory(kMenuDirectories[i], &path) != B_OK)
			continue;
		if (kMenuDirectories[i] != B_USER_DESKBAR_DIRECTORY
			&& path.Append("deskbar/menu") != B_OK) {
			continue;
		}
		// libroot2 maps the non-packaged directories onto the others
		if (!paths.HasString(path.Path()))
			paths.Add(path.Path());
	}
	if (paths.IsEmpty())
		return B_ENTRY_NOT_FOUND;

	BString content(kGeneratedMarker);
	content << "\n";
	for (int32 i = 0; i < paths.CountStrings(); i++) {
		BString path(paths.StringAt(i));
		path.CharacterEscape("\\\"", '\\');
		content << "directory \"" << path << "\"\n";
	}

	BPath filePath;
	status_t error = GetDeskbarSettingsDirectory(filePath, true);
	if (error == B_OK)
		error = filePath.Append(kDeskbarMenuEntriesFileName);
	if (error != B_OK)
		return error;

	BFile file;
	if (file.SetTo(filePath.Path(), B_READ_ONLY) == B_OK) {
		off_t size = 0;
		if (file.GetSize(&size) != B_OK || size > 64 * 1024)
			return B_OK;

		BString existing;
		char* buffer = existing.LockBuffer((int32)size + 1);
		if (buffer == NULL)
			return B_NO_MEMORY;
		ssize_t bytesRead = file.Read(buffer, (size_t)size);
		existing.UnlockBuffer(bytesRead > 0 ? (int32)bytesRead : 0);

		if (existing == content)
			return B_OK;
		if (!existing.StartsWith(kGeneratedMarker))
			return B_OK;
				// the user's own definition
	}

	error = file.SetTo(filePath.Path(),
		B_WRITE_ONLY | B_CREATE_FILE | B_ERASE_FILE);
	if (error != B_OK)
		return error;

	ssize_t written = file.Write(content.String(), content.Length());
	if (written != content.Length())
		return written < 0 ? (status_t)written : B_IO_ERROR;

	// Tracker's kVirtualDirectoryMimeType; headers/private/storage has a
	// MimeTypes.h of its own that shadows Tracker's
	BNodeInfo(&file).SetType("application/x-vnd.vos-virtual-directory");
	return B_OK;
}
